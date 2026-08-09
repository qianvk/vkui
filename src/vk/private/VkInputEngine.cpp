#include "VkInputEngine.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vkui::vk::detail {
namespace {

constexpr std::uint32_t kMaximumUnicodeScalar = 0x10ffffU;
constexpr std::uint32_t kSpecialKeyBase =
    kMaximumUnicodeScalar + 1U;
constexpr std::size_t kMaximumMappingDepth = 1'000;
// Neovim resets mapdepth after a literal character is returned. Its editor
// loop can therefore be interrupted between characters, while vkery drains a
// resolved command transaction inside one Qt event. Keep the same mapdepth
// rule and add a transaction-wide expansion budget so mappings such as
// `h -> hh` cannot monopolize the GUI thread forever.
constexpr std::size_t kMaximumMappingChainExpansions = 1'000;

[[nodiscard]] MappingModes modeBit(const MappingMode mode) noexcept
{
    return mappingModes(mode);
}

[[nodiscard]] bool containsMode(
    const MappingModes modes,
    const MappingMode mode) noexcept
{
    return (modes & modeBit(mode)) != 0;
}

[[nodiscard]] constexpr std::size_t modeSlot(
    const MappingMode mode) noexcept
{
    switch (mode) {
    case MappingMode::Normal:
        return 0;
    case MappingMode::Insert:
        return 1;
    case MappingMode::Visual:
        return 2;
    case MappingMode::OperatorPending:
        return 3;
    }
    return 0;
}

constexpr std::array<MappingMode, 4> kMappingModes{
    MappingMode::Normal,
    MappingMode::Insert,
    MappingMode::Visual,
    MappingMode::OperatorPending};

[[nodiscard]] char32_t asciiLower(const char32_t value) noexcept
{
    return value >= U'A' && value <= U'Z'
        ? value + (U'a' - U'A')
        : value;
}

[[nodiscard]] bool asciiEqual(
    const std::u32string_view lhs,
    const std::u32string_view rhs) noexcept
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (asciiLower(lhs[index]) != asciiLower(rhs[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool isAsciiDigit(const char32_t value) noexcept
{
    return value >= U'0' && value <= U'9';
}

[[nodiscard]] bool isStableSemanticId(
    const std::string_view value) noexcept
{
    return !value.empty()
        && std::ranges::all_of(
            value,
            [](const char character) {
                return (character >= 'a'
                        && character <= 'z')
                    || (character >= 'A'
                        && character <= 'Z')
                    || (character >= '0'
                        && character <= '9')
                    || character == '.'
                    || character == '_'
                    || character == '-'
                    || character == ':';
            });
}

[[nodiscard]] bool startsWith(
    const KeySequence &value,
    const KeySequence &prefix) noexcept
{
    return value.size() >= prefix.size()
        && std::equal(prefix.cbegin(), prefix.cend(), value.cbegin());
}

struct KeyAtomHash final
{
    [[nodiscard]] std::size_t operator()(
        const KeyAtom &key) const noexcept
    {
        const std::size_t code =
            std::hash<std::uint32_t>{}(key.code);
        const std::size_t modifiers =
            std::hash<std::uint8_t>{}(
                static_cast<std::uint8_t>(key.modifiers));
        return code ^ (modifiers
                       + 0x9e3779b9U
                       + (code << 6U)
                       + (code >> 2U));
    }
};

struct KeyTarget final
{
    KeySequence keys;
};

using ResolvedTarget =
    std::variant<
        KeyTarget,
        HostActionTarget,
        CommandTarget>;

struct Mapping final
{
    MappingId id = 0;
    MappingModes modes = 0;
    KeySequence lhs;
    ResolvedTarget target;
    MappingOptions options;
    std::optional<BufferId> buffer;
    MappingMetadata metadata;
    std::optional<WindowId> window;
};

using MappingIndex = std::size_t;

struct TrieNode final
{
    std::unordered_map<
        KeyAtom,
        std::unique_ptr<TrieNode>,
        KeyAtomHash> children;
    std::vector<MappingIndex> terminals;
    std::array<std::optional<MappingIndex>, 4>
        representatives;
    // Latest mapping strictly below this node, indexed by mode. This keeps
    // Neovim's definition-order interaction between <nowait> exact matches
    // and longer partial mappings without scanning the subtree.
    std::array<std::optional<MappingIndex>, 4>
        latestDescendants;
    MappingModes subtreeModes = 0;
};

using Bucket = TrieNode;

enum class MappingScope : std::uint8_t
{
    None,
    Global,
    Buffer,
    Window
};

struct LookupResult final
{
    std::optional<MappingIndex> full;
    std::size_t fullLength = 0;
    bool partial = false;
    MappingScope partialScope = MappingScope::None;
};

[[nodiscard]] bool isInsertModeExitKey(
    const KeyAtom &key) noexcept
{
    if (isSpecialKey(key, SpecialKey::Escape)) {
        return true;
    }
    const auto scalar = character(key);
    return key.modifiers == KeyModifier::Control
        && scalar
        && (*scalar == U'c' || *scalar == U'[');
}

[[nodiscard]] bool isSupportedInsertRhsKey(
    const KeyAtom &key) noexcept
{
    if (key.modifiers == KeyModifier::None) {
        if (character(key)) {
            return true;
        }
        return isSpecialKey(key, SpecialKey::Escape)
            || isSpecialKey(key, SpecialKey::Enter)
            || isSpecialKey(key, SpecialKey::Tab)
            || isSpecialKey(key, SpecialKey::Backspace)
            || isSpecialKey(key, SpecialKey::Delete)
            || isSpecialKey(key, SpecialKey::Left)
            || isSpecialKey(key, SpecialKey::Right)
            || isSpecialKey(key, SpecialKey::Up)
            || isSpecialKey(key, SpecialKey::Down)
            || isSpecialKey(key, SpecialKey::PageUp)
            || isSpecialKey(key, SpecialKey::PageDown)
            || isSpecialKey(key, SpecialKey::Home)
            || isSpecialKey(key, SpecialKey::End);
    }
    if (isInsertModeExitKey(key)) {
        return true;
    }
    const auto scalar = character(key);
    return key.modifiers == KeyModifier::Super
        && scalar
        && (*scalar == U'a'
            || *scalar == U'c'
            || *scalar == U'v'
            || *scalar == U'x'
            || *scalar == U'y'
            || *scalar == U'z');
}

[[nodiscard]] std::optional<SpecialKey> namedSpecialKey(
    const std::u32string_view name)
{
    struct NamedKey final
    {
        std::u32string_view name;
        SpecialKey key;
    };
    static constexpr std::array<NamedKey, 15> namedKeys{{
        {U"esc", SpecialKey::Escape},
        {U"escape", SpecialKey::Escape},
        {U"cr", SpecialKey::Enter},
        {U"enter", SpecialKey::Enter},
        {U"return", SpecialKey::Enter},
        {U"bs", SpecialKey::Backspace},
        {U"backspace", SpecialKey::Backspace},
        {U"del", SpecialKey::Delete},
        {U"delete", SpecialKey::Delete},
        {U"tab", SpecialKey::Tab},
        {U"left", SpecialKey::Left},
        {U"right", SpecialKey::Right},
        {U"up", SpecialKey::Up},
        {U"down", SpecialKey::Down},
        {U"insert", SpecialKey::Insert},
    }};
    const auto found = std::find_if(
        namedKeys.cbegin(), namedKeys.cend(),
        [name](const NamedKey &candidate) {
            return asciiEqual(candidate.name, name);
        });
    if (found != namedKeys.cend()) {
        return found->key;
    }
    if (asciiEqual(name, U"home")) {
        return SpecialKey::Home;
    }
    if (asciiEqual(name, U"end")) {
        return SpecialKey::End;
    }
    if (asciiEqual(name, U"pageup")
        || asciiEqual(name, U"pgup")) {
        return SpecialKey::PageUp;
    }
    if (asciiEqual(name, U"pagedown")
        || asciiEqual(name, U"pgdown")) {
        return SpecialKey::PageDown;
    }
    if (name.size() >= 2
        && asciiLower(name.front()) == U'f') {
        unsigned value = 0;
        for (const char32_t character : name.substr(1)) {
            if (!isAsciiDigit(character)) {
                return std::nullopt;
            }
            value = value * 10U
                + static_cast<unsigned>(character - U'0');
        }
        if (value >= 1U && value <= 35U) {
            return static_cast<SpecialKey>(
                static_cast<unsigned>(SpecialKey::F1)
                + value - 1U);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SpecialKey> specialValue(
    const KeyAtom &key) noexcept
{
    const std::uint32_t first =
        kSpecialKeyBase
        + static_cast<std::uint32_t>(SpecialKey::Escape);
    const std::uint32_t last =
        kSpecialKeyBase
        + static_cast<std::uint32_t>(SpecialKey::F35);
    if (key.code < first || key.code > last) {
        return std::nullopt;
    }
    return static_cast<SpecialKey>(
        key.code - kSpecialKeyBase);
}

[[nodiscard]] std::u32string specialDisplayName(
    const SpecialKey key)
{
    switch (key) {
    case SpecialKey::Escape:
        return U"Esc";
    case SpecialKey::Enter:
        return U"CR";
    case SpecialKey::Backspace:
        return U"BS";
    case SpecialKey::Delete:
        return U"Del";
    case SpecialKey::Tab:
        return U"Tab";
    case SpecialKey::Left:
        return U"Left";
    case SpecialKey::Right:
        return U"Right";
    case SpecialKey::Up:
        return U"Up";
    case SpecialKey::Down:
        return U"Down";
    case SpecialKey::Home:
        return U"Home";
    case SpecialKey::End:
        return U"End";
    case SpecialKey::PageUp:
        return U"PageUp";
    case SpecialKey::PageDown:
        return U"PageDown";
    case SpecialKey::Insert:
        return U"Insert";
    default:
        break;
    }

    const unsigned functionNumber =
        static_cast<unsigned>(key)
        - static_cast<unsigned>(SpecialKey::F1)
        + 1U;
    std::u32string result{U'F'};
    if (functionNumber >= 10U) {
        result.push_back(
            static_cast<char32_t>(
                U'0' + functionNumber / 10U));
    }
    result.push_back(
        static_cast<char32_t>(
            U'0' + functionNumber % 10U));
    return result;
}

[[nodiscard]] std::u32string keyDisplayNotation(
    const KeyAtom &key)
{
    const std::optional<char32_t> scalar = character(key);
    const std::optional<SpecialKey> special =
        specialValue(key);
    if (key.modifiers == KeyModifier::None && scalar) {
        if (*scalar == U' ') {
            return U"<Space>";
        }
        if (*scalar == U'<') {
            return U"<lt>";
        }
        return std::u32string(1, *scalar);
    }

    std::u32string result{U'<'};
    if (hasModifier(key.modifiers, KeyModifier::Control)) {
        result += U"C-";
    }
    if (hasModifier(key.modifiers, KeyModifier::Alt)) {
        result += U"A-";
    }
    if (hasModifier(key.modifiers, KeyModifier::Shift)) {
        result += U"S-";
    }
    if (hasModifier(key.modifiers, KeyModifier::Super)) {
        result += U"D-";
    }
    if (special) {
        result += specialDisplayName(*special);
    } else if (scalar) {
        result.push_back(*scalar);
    } else {
        result += U"Unknown";
    }
    result.push_back(U'>');
    return result;
}

[[nodiscard]] std::u32string sequenceDisplayNotation(
    const KeySequence &keys)
{
    std::u32string result;
    for (const KeyAtom &key : keys) {
        result += keyDisplayNotation(key);
    }
    return result;
}

} // namespace

KeyAtom characterKey(
    const char32_t value,
    const KeyModifier modifiers) noexcept
{
    return KeyAtom{
        static_cast<std::uint32_t>(value),
        modifiers};
}

KeyAtom specialKey(
    const SpecialKey value,
    const KeyModifier modifiers) noexcept
{
    return KeyAtom{
        kSpecialKeyBase + static_cast<std::uint32_t>(value),
        modifiers};
}

bool isSpecialKey(
    const KeyAtom &key,
    const SpecialKey value) noexcept
{
    return key.code
            == kSpecialKeyBase + static_cast<std::uint32_t>(value)
        && key.modifiers == KeyModifier::None;
}

std::optional<char32_t> character(
    const KeyAtom &key) noexcept
{
    return key.code <= kMaximumUnicodeScalar
        ? std::optional<char32_t>(
              static_cast<char32_t>(key.code))
        : std::nullopt;
}

class InputEngine::Implementation final
{
public:
    [[nodiscard]] ParseResult parse(
        const std::u32string_view notation) const
    {
        ParseResult result;
        result.keys.reserve(notation.size());
        std::size_t cursor = 0;
        while (cursor < notation.size()) {
            if (notation[cursor] != U'<') {
                result.keys.push_back(
                    characterKey(notation[cursor++]));
                continue;
            }
            const std::size_t closing =
                notation.find(U'>', cursor + 1);
            if (closing == std::u32string_view::npos) {
                result.error =
                    "unterminated key notation";
                return result;
            }
            const std::u32string_view token =
                notation.substr(
                    cursor + 1,
                    closing - cursor - 1);
            if (token.empty()) {
                result.error = "empty key notation";
                return result;
            }
            if (asciiEqual(token, U"leader")) {
                result.keys.insert(
                    result.keys.end(),
                    leader.cbegin(),
                    leader.cend());
                cursor = closing + 1;
                continue;
            }
            if (asciiEqual(token, U"localleader")) {
                result.keys.insert(
                    result.keys.end(),
                    localLeader.cbegin(),
                    localLeader.cend());
                cursor = closing + 1;
                continue;
            }
            if (asciiEqual(token, U"space")) {
                result.keys.push_back(characterKey(U' '));
                cursor = closing + 1;
                continue;
            }
            if (asciiEqual(token, U"lt")) {
                result.keys.push_back(characterKey(U'<'));
                cursor = closing + 1;
                continue;
            }

            KeyModifier modifiers = KeyModifier::None;
            std::size_t nameStart = 0;
            while (nameStart + 2 < token.size()
                   && token[nameStart + 1] == U'-') {
                const char32_t prefix =
                    asciiLower(token[nameStart]);
                switch (prefix) {
                case U'c':
                    modifiers =
                        modifiers | KeyModifier::Control;
                    break;
                case U'a':
                case U'm':
                    modifiers =
                        modifiers | KeyModifier::Alt;
                    break;
                case U's':
                    modifiers =
                        modifiers | KeyModifier::Shift;
                    break;
                case U'd':
                    modifiers =
                        modifiers | KeyModifier::Super;
                    break;
                default:
                    nameStart = token.size();
                    break;
                }
                if (nameStart == token.size()) {
                    break;
                }
                nameStart += 2;
            }
            if (nameStart >= token.size()) {
                result.error =
                    "unknown key modifier in notation";
                return result;
            }
            const std::u32string_view name =
                token.substr(nameStart);
            if (name.size() == 1) {
                char32_t value = name.front();
                if (hasModifier(
                        modifiers,
                        KeyModifier::Control)
                    && !hasModifier(
                        modifiers,
                        KeyModifier::Shift)) {
                    value = asciiLower(value);
                } else if (hasModifier(
                               modifiers,
                               KeyModifier::Shift)
                           && value >= U'a'
                           && value <= U'z'
                           && modifiers
                                  == KeyModifier::Shift) {
                    value -= U'a' - U'A';
                    modifiers =
                        modifiers & ~KeyModifier::Shift;
                }
                result.keys.push_back(
                    characterKey(value, modifiers));
                cursor = closing + 1;
                continue;
            }
            const auto special = namedSpecialKey(name);
            if (!special) {
                result.error =
                    "unknown key name in notation";
                return result;
            }
            result.keys.push_back(
                specialKey(*special, modifiers));
            cursor = closing + 1;
        }
        return result;
    }

    [[nodiscard]] const Mapping *mappingAt(
        const MappingIndex index) const noexcept
    {
        return index < mappings.size() && mappings[index]
            ? &*mappings[index]
            : nullptr;
    }

    [[nodiscard]] std::optional<MappingIndex>
    terminalForMode(
        const TrieNode &node,
        const MappingMode mode) const
    {
        for (auto terminal = node.terminals.crbegin();
             terminal != node.terminals.crend();
             ++terminal) {
            const Mapping *const mapping =
                mappingAt(*terminal);
            if (mapping != nullptr
                && containsMode(mapping->modes, mode)) {
                return *terminal;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool hasChildForMode(
        const TrieNode &node,
        const MappingMode mode) const noexcept
    {
        return std::ranges::any_of(
            node.children,
            [mode](const auto &child) {
                return containsMode(
                    child.second->subtreeModes,
                    mode);
            });
    }

    [[nodiscard]] const TrieNode *nodeForPrefix(
        const Bucket &bucket,
        const std::deque<TypeaheadEntry> &pendingEntries) const
    {
        const TrieNode *node = &bucket;
        for (const TypeaheadEntry &entry : pendingEntries) {
            if (entry.permission != MapPermission::Allow) {
                return nullptr;
            }
            const auto child = node->children.find(entry.key);
            if (child == node->children.cend()) {
                return nullptr;
            }
            node = child->second.get();
        }
        return node;
    }

    [[nodiscard]] bool hasExactMappingAtPrefix(
        const MappingMode mode,
        const std::optional<BufferId> buffer,
        const std::optional<WindowId> window,
        const std::deque<TypeaheadEntry> &pending) const
    {
        const auto bucketHasExact =
            [this, mode, &pending](
                const Bucket &bucket) {
                const TrieNode *const node =
                    nodeForPrefix(bucket, pending);
                return node != nullptr
                    && terminalForMode(
                           *node, mode)
                        .has_value();
            };
        if (window) {
            const auto local =
                windowBuckets.find(*window);
            if (local != windowBuckets.cend()
                && bucketHasExact(local->second)) {
                return true;
            }
        }
        if (buffer) {
            const auto local =
                localBuckets.find(*buffer);
            if (local != localBuckets.cend()
                && bucketHasExact(local->second)) {
                return true;
            }
        }
        return bucketHasExact(globalBuckets);
    }

    [[nodiscard]] bool scanBucket(
        const Bucket &bucket,
        const MappingScope scope,
        const MappingMode mode,
        const std::deque<TypeaheadEntry> &pendingEntries,
        const bool timedOut,
        LookupResult &result) const
    {
        if (pendingEntries.empty()) {
            return true;
        }

        struct ScanEvent final
        {
            MappingIndex mapping = 0;
            bool partial = false;
        };
        std::vector<ScanEvent> events;
        events.reserve(pendingEntries.size() + 1);

        const TrieNode *node = &bucket;
        bool consumedEntireTypeahead = true;
        for (const TypeaheadEntry &entry : pendingEntries) {
            if (entry.permission != MapPermission::Allow) {
                consumedEntireTypeahead = false;
                break;
            }
            const auto child = node->children.find(entry.key);
            if (child == node->children.cend()) {
                consumedEntireTypeahead = false;
                break;
            }
            node = child->second.get();

            const std::optional<MappingIndex> terminal =
                terminalForMode(*node, mode);
            if (terminal) {
                events.push_back(
                    ScanEvent{*terminal, false});
            }
        }

        if (!timedOut && consumedEntireTypeahead) {
            const std::optional<MappingIndex> partial =
                node->latestDescendants[modeSlot(mode)];
            if (partial) {
                events.push_back(
                    ScanEvent{*partial, true});
            }
        }

        std::ranges::sort(
            events,
            [](const ScanEvent &lhs,
               const ScanEvent &rhs) {
                return lhs.mapping > rhs.mapping;
            });
        for (const ScanEvent &event : events) {
            if (event.partial) {
                const Mapping *const matched = result.full
                    ? mappingAt(*result.full)
                    : nullptr;
                // Neovim walks mappings in reverse definition order. A
                // partial match stops that walk unless a newer full match
                // already won with <nowait>.
                if (matched == nullptr
                    || !matched->options.nowait) {
                    result.partial = true;
                    result.partialScope = scope;
                    return false;
                }
                continue;
            }

            const Mapping *const mapping =
                mappingAt(event.mapping);
            if (mapping != nullptr
                && (!result.full
                    || mapping->lhs.size()
                        > result.fullLength)) {
                result.full = event.mapping;
                result.fullLength =
                    mapping->lhs.size();
            }
        }
        return true;
    }

    [[nodiscard]] LookupResult lookup(
        const MappingMode mode,
        const std::optional<BufferId> buffer,
        const std::optional<WindowId> window,
        const std::deque<TypeaheadEntry> &pendingEntries,
        const bool timedOut) const
    {
        LookupResult result;
        if (pendingEntries.empty()) {
            return result;
        }
        if (window) {
            const auto foundWindow =
                windowBuckets.find(*window);
            if (foundWindow != windowBuckets.cend()) {
                if (!scanBucket(
                        foundWindow->second,
                        MappingScope::Window,
                        mode,
                        pendingEntries,
                        timedOut,
                        result)) {
                    return result;
                }
            }
        }
        if (buffer) {
            const auto foundLocal =
                localBuckets.find(*buffer);
            if (foundLocal != localBuckets.cend()) {
                if (!scanBucket(
                        foundLocal->second,
                        MappingScope::Buffer,
                        mode,
                        pendingEntries,
                        timedOut,
                        result)) {
                    return result;
                }
            }
        }
        static_cast<void>(
            scanBucket(
                globalBuckets,
                MappingScope::Global,
                mode,
                pendingEntries,
                timedOut,
                result));
        return result;
    }

    [[nodiscard]] bool addToBucket(
        const MappingIndex index,
        const Mapping &mapping)
    {
        if (mapping.lhs.empty()) {
            return false;
        }
        Bucket &bucket = mapping.window
            ? windowBuckets[*mapping.window]
            : mapping.buffer
                ? localBuckets[*mapping.buffer]
                : globalBuckets;
        TrieNode *node = &bucket;
        node->subtreeModes = static_cast<MappingModes>(
            node->subtreeModes | mapping.modes);
        for (const KeyAtom &key : mapping.lhs) {
            for (const MappingMode mode : kMappingModes) {
                if (containsMode(mapping.modes, mode)) {
                    node->latestDescendants[
                        modeSlot(mode)] = index;
                }
            }
            auto [child, inserted] =
                node->children.try_emplace(key);
            if (inserted) {
                child->second =
                    std::make_unique<TrieNode>();
            }
            node = child->second.get();
            node->subtreeModes =
                static_cast<MappingModes>(
                    node->subtreeModes | mapping.modes);
            for (const MappingMode mode : kMappingModes) {
                if (containsMode(mapping.modes, mode)) {
                    node->representatives[modeSlot(mode)] =
                        index;
                }
            }
        }
        node->terminals.push_back(index);
        return true;
    }

    void rebuildBuckets()
    {
        mappings.erase(
            std::remove_if(
                mappings.begin(),
                mappings.end(),
                [](const std::optional<Mapping> &mapping) {
                    return !mapping;
                }),
            mappings.end());
        globalBuckets = Bucket{};
        localBuckets.clear();
        windowBuckets.clear();
        for (MappingIndex index = 0;
             index < mappings.size();
             ++index) {
            if (mappings[index]
                && mappings[index]->modes != 0) {
                static_cast<void>(
                    addToBucket(index, *mappings[index]));
            }
        }
    }

    void appendPrefixCandidates(
        const Bucket &bucket,
        const MappingMode mode,
        const std::deque<TypeaheadEntry> &pending,
        const bool bufferLocal,
        const bool windowLocal,
        const std::u32string &prefixNotation,
        std::unordered_set<KeyAtom, KeyAtomHash> &seen,
        std::vector<MappingPrefixCandidate> &result) const
    {
        const TrieNode *const node =
            nodeForPrefix(bucket, pending);
        if (node == nullptr) {
            return;
        }

        for (const auto &[key, childStorage] :
             node->children) {
            const TrieNode &child = *childStorage;
            if (!containsMode(child.subtreeModes, mode)
                || seen.contains(key)) {
                continue;
            }

            const std::optional<MappingIndex> terminal =
                terminalForMode(child, mode);
            const std::optional<MappingIndex> representative =
                terminal
                ? terminal
                : child.representatives[modeSlot(mode)];
            if (!representative) {
                continue;
            }
            const Mapping *const mapping =
                mappingAt(*representative);
            if (mapping == nullptr) {
                continue;
            }

            seen.insert(key);
            const std::u32string keyNotation =
                keyDisplayNotation(key);
            std::string commandId;
            if (terminal) {
                const Mapping *const complete =
                    mappingAt(*terminal);
                if (complete != nullptr) {
                    if (const auto *command =
                            std::get_if<CommandTarget>(
                                &complete->target)) {
                        commandId = command->id;
                    }
                }
            }
            result.push_back(
                MappingPrefixCandidate{
                    keyNotation,
                    prefixNotation + keyNotation,
                    mapping->id,
                    terminal.has_value(),
                    hasChildForMode(child, mode),
                    bufferLocal,
                    windowLocal,
                    InputHintOrigin::Mapping,
                    mapping->metadata.description,
                    mapping->metadata.group,
                    mapping->metadata.sourcePlugin,
                    mapping->metadata.hidden,
                    mapping->metadata.order,
                    std::move(commandId)});
        }
    }

    [[nodiscard]] std::optional<MappingPrefixSnapshot>
    prefixSnapshot(
        const MappingMode mode,
        const std::optional<BufferId> buffer,
        const std::optional<WindowId> window,
        const std::uint64_t generation,
        const bool root = false) const
    {
        if (!root && (!waiting || typeahead.empty())) {
            return std::nullopt;
        }

        const std::deque<TypeaheadEntry> rootTypeahead;
        const std::deque<TypeaheadEntry> &activeTypeahead =
            root ? rootTypeahead : typeahead;

        if (!root) {
            const LookupResult current =
                lookup(
                    mode,
                    buffer,
                    window,
                    activeTypeahead,
                    false);
            if (!current.partial) {
                return std::nullopt;
            }
        }

        MappingPrefixSnapshot snapshot;
        snapshot.mode = mode;
        snapshot.buffer = buffer;
        snapshot.window = window;
        snapshot.generation = generation;
        KeySequence prefix;
        prefix.reserve(activeTypeahead.size());
        for (const TypeaheadEntry &entry :
             activeTypeahead) {
            if (entry.permission != MapPermission::Allow) {
                return std::nullopt;
            }
            prefix.push_back(entry.key);
        }
        snapshot.prefixNotation =
            sequenceDisplayNotation(prefix);
        const auto isLeaderRooted =
            [&prefix](const KeySequence &leaderKeys) {
                return !leaderKeys.empty()
                    && startsWith(prefix, leaderKeys);
            };
        const bool hasExact = !root
            && hasExactMappingAtPrefix(
                mode,
                buffer,
                window,
                activeTypeahead);
        // A plain Leader prefix may remain open until the user chooses a
        // continuation.  If that same node is also an exact mapping, however,
        // it is the ordinary Vim exact+longer ambiguity: timeoutlen commits
        // the short mapping while a key received before the deadline may
        // still select a descendant.  Treating that case as a persistent
        // Leader would make the exact mapping unreachable forever.
        snapshot.waitPolicy = root
            ? InputHintWaitPolicy::PersistentLeader
            : (isLeaderRooted(leader)
                   || isLeaderRooted(localLeader))
                    && !hasExact
            ? InputHintWaitPolicy::PersistentLeader
            : InputHintWaitPolicy::TimedMapping;
        snapshot.nativeFallbackReachable = !root && !hasExact;

        std::unordered_set<KeyAtom, KeyAtomHash> seen;
        if (window) {
            const auto local =
                windowBuckets.find(*window);
            if (local != windowBuckets.cend()) {
                appendPrefixCandidates(
                    local->second,
                    mode,
                    activeTypeahead,
                    false,
                    true,
                    snapshot.prefixNotation,
                    seen,
                    snapshot.candidates);
            }
        }
        if (buffer) {
            const auto local = localBuckets.find(*buffer);
            if (local != localBuckets.cend()) {
                appendPrefixCandidates(
                    local->second,
                    mode,
                    activeTypeahead,
                    true,
                    false,
                    snapshot.prefixNotation,
                    seen,
                    snapshot.candidates);
            }
        }
        appendPrefixCandidates(
            globalBuckets,
            mode,
            activeTypeahead,
            false,
            false,
            snapshot.prefixNotation,
            seen,
            snapshot.candidates);

        if (snapshot.candidates.empty()) {
            return std::nullopt;
        }
        std::ranges::sort(
            snapshot.candidates,
            [](const MappingPrefixCandidate &lhs,
               const MappingPrefixCandidate &rhs) {
                const auto locality = [](
                    const MappingPrefixCandidate &candidate) {
                    return candidate.windowLocal
                        ? 0
                        : candidate.bufferLocal
                        ? 1
                        : 2;
                };
                if (locality(lhs) != locality(rhs)) {
                    // Mirrors which-key.nvim's `local` sorter while
                    // preserving VkCore's stronger window-local scope.
                    return locality(lhs) < locality(rhs);
                }
                if (lhs.order != rhs.order) {
                    return lhs.order < rhs.order;
                }
                if (lhs.keyNotation != rhs.keyNotation) {
                    return lhs.keyNotation
                        < rhs.keyNotation;
                }
                return lhs.mappingId < rhs.mappingId;
            });
        return snapshot;
    }

    static void prependMappedTo(
        std::deque<TypeaheadEntry> &target,
        const Mapping &mapping,
        const KeySequence &keys)
    {
        const bool skipFirst =
            mapping.options.remap == RemapPolicy::Recursive
            && startsWith(keys, mapping.lhs);
        for (std::size_t reverse = keys.size();
             reverse > 0;
             --reverse) {
            const std::size_t index = reverse - 1;
            MapPermission permission = MapPermission::Allow;
            if (mapping.options.remap == RemapPolicy::NoRemap
                || (skipFirst && index == 0)) {
                permission = MapPermission::Deny;
            }
            target.push_front(
                TypeaheadEntry{
                    keys[index],
                    InputOrigin::Mapping,
                    permission,
                    mapping.options.silent,
                    0});
        }
    }

    static void prependRepeatPrefixTo(
        std::deque<TypeaheadEntry> &target,
        const Mapping &mapping)
    {
        // The final lhs atom is the repeat selector. Retaining the declarative
        // prefix in typeahead makes the normal trie resolver authoritative for
        // every subsequent key; no Qt event or command id is special-cased.
        for (std::size_t reverse = mapping.lhs.size() - 1;
             reverse > 0;
             --reverse) {
            target.push_front(
                TypeaheadEntry{
                    mapping.lhs[reverse - 1],
                    InputOrigin::RepeatPrefix,
                    MapPermission::Allow,
                    mapping.options.silent,
                    0});
        }
    }

    void prependRepeatPrefix(const Mapping &mapping)
    {
        prependRepeatPrefixTo(typeahead, mapping);
    }

    void prependMapped(
        const Mapping &mapping,
        const KeySequence &keys)
    {
        prependMappedTo(typeahead, mapping, keys);
    }

    static void discardGeneratedPrefixFrom(
        std::deque<TypeaheadEntry> &target)
    {
        while (!target.empty()
               && (target.front().origin
                       == InputOrigin::Mapping
                   || target.front().origin
                       == InputOrigin::RepeatPrefix)) {
            target.pop_front();
        }
    }

    void discardGeneratedPrefix()
    {
        discardGeneratedPrefixFrom(typeahead);
    }

    KeySequence leader{characterKey(U'\\')};
    KeySequence localLeader{characterKey(U'\\')};
    std::vector<std::optional<Mapping>> mappings;
    Bucket globalBuckets;
    std::unordered_map<BufferId, Bucket> localBuckets;
    std::unordered_map<WindowId, Bucket> windowBuckets;
    std::deque<TypeaheadEntry> typeahead;
    MappingId nextMappingId = 1;
    std::uint64_t nextInputToken = 1;
    std::size_t expansionDepth = 0;
    std::size_t chainExpansionCount = 0;
    bool waiting = false;
};

InputEngine::InputEngine()
    : m_impl(std::make_unique<Implementation>())
{
}

InputEngine::~InputEngine() = default;

void InputEngine::setLeader(KeySequence leader)
{
    m_impl->leader = leader.empty()
        ? KeySequence{characterKey(U'\\')}
        : std::move(leader);
}

void InputEngine::setLocalLeader(KeySequence leader)
{
    m_impl->localLeader = leader.empty()
        ? KeySequence{characterKey(U'\\')}
        : std::move(leader);
}

ParseResult InputEngine::parse(
    const std::u32string_view notation) const
{
    return m_impl->parse(notation);
}

MappingId InputEngine::addMapping(
    const MappingDefinition &definition,
    std::string *const error)
{
    const std::span<const MappingDefinition> definitions(
        &definition, 1);
    auto ids = addMappings(definitions, error);
    return ids.empty() ? MappingId{0} : ids.front();
}

std::vector<MappingId> InputEngine::addMappings(
    const std::span<const MappingDefinition> definitions,
    std::string *const error)
{
    if (definitions.empty()) {
        if (error != nullptr) {
            *error = "mapping batch must not be empty";
        }
        return {};
    }
    if (m_impl->nextMappingId == 0
        || definitions.size()
            > std::numeric_limits<MappingId>::max()
                - m_impl->nextMappingId + 1) {
        if (error != nullptr) {
            *error = "mapping id space is exhausted";
        }
        return {};
    }

    // Stage both replacements and new definitions away from the live tries.
    // Validation failure therefore cannot expose a prefix from a partial
    // plugin generation or consume mapping ids.
    auto stagedMappings = m_impl->mappings;
    MappingId stagedNextMappingId = m_impl->nextMappingId;
    std::vector<MappingId> ids;
    ids.reserve(definitions.size());

    constexpr MappingModes supportedModes =
        MappingMode::Normal | MappingMode::Insert
        | MappingMode::Visual | MappingMode::OperatorPending;
    const auto fail = [error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return std::vector<MappingId>{};
    };

    for (const MappingDefinition &definition : definitions) {
        if (definition.modes == 0
            || (definition.modes
                & static_cast<MappingModes>(~supportedModes)) != 0) {
            return fail("mapping modes are empty or unsupported");
        }
        if (definition.buffer && definition.window) {
            return fail(
                "mapping cannot be both buffer-local and window-local");
        }
        const ParseResult lhs = parse(definition.lhs);
        if (!lhs || lhs.keys.empty()) {
            return fail(lhs.error.empty()
                    ? "mapping lhs cannot be empty"
                    : lhs.error);
        }
        if (definition.options.repeatPrefix
            && (lhs.keys.size() < 2
                || !std::holds_alternative<CommandTarget>(
                    definition.target))) {
            return fail(
                "repeat_prefix requires a command mapping with at least two lhs keys");
        }

        ResolvedTarget target;
        if (const auto *keyTarget =
                std::get_if<KeySequenceTarget>(
                    &definition.target)) {
            const ParseResult rhs = parse(keyTarget->notation);
            if (!rhs) {
                return fail(rhs.error);
            }
            if (containsMode(
                    definition.modes,
                    MappingMode::Insert)) {
                const bool unsupported = std::any_of(
                    rhs.keys.cbegin(),
                    rhs.keys.cend(),
                    [](const KeyAtom &key) {
                        return !isSupportedInsertRhsKey(key);
                    });
                const std::size_t exits =
                    static_cast<std::size_t>(
                        std::count_if(
                            rhs.keys.cbegin(),
                            rhs.keys.cend(),
                            isInsertModeExitKey));
                if (unsupported
                    || (exits != 0 && rhs.keys.size() != 1)) {
                    return fail(
                        "Insert mapping rhs cannot mix a mode exit "
                        "with other keys or use an unsupported key");
                }
            }
            target = KeyTarget{rhs.keys};
        } else if (const auto *action =
                       std::get_if<HostActionTarget>(
                           &definition.target)) {
            if (action->action == HostAction::None) {
                return fail(
                    "mapping host action target cannot be empty");
            }
            target = *action;
        } else {
            const CommandTarget &command =
                std::get<CommandTarget>(definition.target);
            if (!isStableSemanticId(command.id)) {
                return fail(
                    "mapping command target must use a valid stable id");
            }
            target = command;
        }

        // Mapping replacement is owner-local. Compare against the staged
        // sequence so collisions within this batch have exactly the same
        // definition-order semantics as repeated addMapping() calls.
        for (const auto &existing : stagedMappings) {
            if (!existing
                || existing->buffer != definition.buffer
                || existing->window != definition.window
                || existing->lhs != lhs.keys
                || (existing->modes & definition.modes) == 0
                || existing->metadata.sourcePlugin
                    == definition.metadata.sourcePlugin) {
                continue;
            }
            if (!existing->metadata.sourcePlugin.empty()
                || !definition.metadata.sourcePlugin.empty()) {
                const auto displayOwner = [](
                    const std::string &owner) -> std::string {
                    return owner.empty()
                        ? std::string("<anonymous>")
                        : owner;
                };
                return fail(
                    "mapping owned by '"
                    + displayOwner(
                        definition.metadata.sourcePlugin)
                    + "' conflicts with mapping owned by '"
                    + displayOwner(
                        existing->metadata.sourcePlugin)
                    + "' in the same scope and mode");
            }
        }

        for (auto &existing : stagedMappings) {
            if (!existing
                || existing->buffer != definition.buffer
                || existing->window != definition.window
                || existing->lhs != lhs.keys) {
                continue;
            }
            existing->modes = static_cast<MappingModes>(
                existing->modes
                & static_cast<MappingModes>(~definition.modes));
            if (existing->modes == 0) {
                existing.reset();
            }
        }

        const MappingId id = stagedNextMappingId++;
        stagedMappings.emplace_back(Mapping{
            id,
            definition.modes,
            lhs.keys,
            std::move(target),
            definition.options,
            definition.buffer,
            definition.metadata,
            definition.window});
        ids.push_back(id);
    }

    m_impl->mappings.swap(stagedMappings);
    m_impl->nextMappingId = stagedNextMappingId;
    m_impl->rebuildBuckets();
    if (error != nullptr) {
        error->clear();
    }
    return ids;
}

bool InputEngine::removeMapping(const MappingId id)
{
    const std::array ids{id};
    return removeMappings(ids) == 1;
}

std::size_t InputEngine::removeMappings(
    const std::span<const MappingId> ids)
{
    if (ids.empty()) {
        return 0;
    }
    const std::unordered_set<MappingId> requested(
        ids.begin(), ids.end());
    std::size_t removed = 0;
    for (auto &mapping : m_impl->mappings) {
        if (mapping && requested.contains(mapping->id)) {
            mapping.reset();
            ++removed;
        }
    }
    if (removed != 0) {
        m_impl->rebuildBuckets();
    }
    return removed;
}

bool InputEngine::pendingResolutionUsesAnyMapping(
    const std::span<const MappingId> ids,
    const MappingMode mode,
    const std::optional<BufferId> buffer,
    const std::optional<WindowId> window) const
{
    if (ids.empty() || !m_impl->waiting
        || m_impl->typeahead.empty()) {
        return false;
    }

    KeySequence prefix;
    prefix.reserve(m_impl->typeahead.size());
    for (const TypeaheadEntry &entry : m_impl->typeahead) {
        if (entry.permission != MapPermission::Allow) {
            return false;
        }
        prefix.push_back(entry.key);
    }

    const LookupResult current = m_impl->lookup(
        mode, buffer, window, m_impl->typeahead, false);
    if (!current.partial) {
        return false;
    }

    const std::unordered_set<MappingId> requested(
        ids.begin(), ids.end());
    if (current.full) {
        const Mapping *const full =
            m_impl->mappingAt(*current.full);
        if (full != nullptr && requested.contains(full->id)) {
            return true;
        }
    }

    const auto belongsToDecisiveScope =
        [scope = current.partialScope, buffer, window](
            const Mapping &mapping) {
            switch (scope) {
            case MappingScope::Window:
                return window && mapping.window == window;
            case MappingScope::Buffer:
                return !mapping.window && buffer
                    && mapping.buffer == buffer;
            case MappingScope::Global:
                return !mapping.window && !mapping.buffer;
            case MappingScope::None:
                return false;
            }
            return false;
        };

    // Every longer mapping below the waiting node can still consume a future
    // key. The exact/short mapping that wins on timeout was handled above.
    for (const auto &mapping : m_impl->mappings) {
        if (!mapping || !requested.contains(mapping->id)
            || !containsMode(mapping->modes, mode)
            || !belongsToDecisiveScope(*mapping)
            || mapping->lhs.size() <= prefix.size()
            || !startsWith(mapping->lhs, prefix)) {
            continue;
        }
        return true;
    }
    return false;
}

std::size_t InputEngine::removeWindowMappings(
    const WindowId window)
{
    std::size_t removed = 0;
    for (auto &mapping : m_impl->mappings) {
        if (mapping && mapping->window == window) {
            mapping.reset();
            ++removed;
        }
    }
    if (removed != 0) {
        m_impl->rebuildBuckets();
    }
    return removed;
}

std::size_t InputEngine::removeBufferMappings(
    const BufferId buffer)
{
    const std::array buffers{buffer};
    return removeBufferMappings(buffers);
}

std::size_t InputEngine::removeBufferMappings(
    const std::span<const BufferId> buffers)
{
    if (buffers.empty()) {
        return 0;
    }
    const std::unordered_set<BufferId> removedBuffers(
        buffers.begin(), buffers.end());
    std::size_t removedCount = 0;
    for (auto &mapping : m_impl->mappings) {
        if (mapping && mapping->buffer
            && removedBuffers.contains(*mapping->buffer)) {
            mapping.reset();
            ++removedCount;
        }
    }
    if (removedCount != 0) {
        m_impl->rebuildBuckets();
    }
    return removedCount;
}

void InputEngine::appendTyped(const KeyAtom &key)
{
    if (m_impl->typeahead.empty()) {
        m_impl->expansionDepth = 0;
        m_impl->chainExpansionCount = 0;
    }
    m_impl->waiting = false;
    m_impl->typeahead.push_back(
        TypeaheadEntry{
            key,
            InputOrigin::Typed,
            MapPermission::Allow,
            false,
            m_impl->nextInputToken++});
}

void InputEngine::appendUnmapped(const KeyAtom &key)
{
    if (m_impl->typeahead.empty()) {
        m_impl->expansionDepth = 0;
        m_impl->chainExpansionCount = 0;
    }
    m_impl->waiting = false;
    m_impl->typeahead.push_back(
        TypeaheadEntry{
            key,
            InputOrigin::Typed,
            MapPermission::Deny,
            false,
            m_impl->nextInputToken++});
}

void InputEngine::prependMacro(
    const KeySequence &keys,
    const std::size_t count)
{
    if (keys.empty() || count == 0) {
        return;
    }
    m_impl->waiting = false;
    for (std::size_t repetition = count;
         repetition > 0;
         --repetition) {
        static_cast<void>(repetition);
        for (std::size_t reverse = keys.size();
             reverse > 0;
             --reverse) {
            m_impl->typeahead.push_front(
                TypeaheadEntry{
                    keys[reverse - 1],
                    InputOrigin::Macro,
                    MapPermission::Allow,
                    false,
                    0});
        }
    }
}

ResolveStep InputEngine::resolve(
    const MappingMode mode,
    const std::optional<BufferId> buffer,
    const bool timedOut,
    const std::optional<WindowId> window)
{
    while (!m_impl->typeahead.empty()) {
        const LookupResult lookup =
            m_impl->lookup(
                mode,
                buffer,
                window,
                m_impl->typeahead,
                timedOut);
        const Mapping *full = nullptr;
        if (lookup.full
            && *lookup.full < m_impl->mappings.size()
            && m_impl->mappings[*lookup.full]) {
            full = &*m_impl->mappings[*lookup.full];
        }
        const bool waitForLonger =
            lookup.partial
            && !timedOut
            && (full == nullptr
                || !full->options.nowait);
        if (waitForLonger) {
            m_impl->waiting = true;
            const auto isLeaderRooted =
                [this](const KeySequence &leaderKeys) {
                    if (leaderKeys.empty()
                        || m_impl->typeahead.size()
                            < leaderKeys.size()) {
                        return false;
                    }
                    return std::equal(
                        leaderKeys.cbegin(),
                        leaderKeys.cend(),
                        m_impl->typeahead.cbegin(),
                        [](const KeyAtom &expected,
                           const TypeaheadEntry &actual) {
                            return actual.permission
                                    == MapPermission::Allow
                                && expected == actual.key;
                        });
                };
            const bool leaderRooted =
                isLeaderRooted(m_impl->leader)
                || isLeaderRooted(m_impl->localLeader);
            const bool hasExact =
                m_impl->hasExactMappingAtPrefix(
                    mode,
                    buffer,
                    window,
                    m_impl->typeahead);
            return ResolveStep{
                ResolveKind::NeedMore,
                {},
                HostAction::None,
                {},
                0,
                {},
                leaderRooted && !hasExact
                    ? InputHintWaitPolicy::
                          PersistentLeader
                    : InputHintWaitPolicy::
                          TimedMapping};
        }
        if (full != nullptr) {
            for (std::size_t index = 0;
                 index < lookup.fullLength;
                 ++index) {
                m_impl->typeahead.pop_front();
            }
            const bool depthExceeded =
                ++m_impl->expansionDepth
                    >= kMaximumMappingDepth;
            const bool transactionExceeded =
                ++m_impl->chainExpansionCount
                    >= kMaximumMappingChainExpansions;
            if (depthExceeded || transactionExceeded) {
                m_impl->discardGeneratedPrefix();
                m_impl->expansionDepth = 0;
                m_impl->chainExpansionCount = 0;
                m_impl->waiting = false;
                return ResolveStep{
                    ResolveKind::Error,
                    {},
                    HostAction::None,
                    "recursive mapping exceeded maxmapdepth",
                    0,
                    {}};
            }
            if (const auto *action =
                    std::get_if<HostActionTarget>(
                        &full->target)) {
                m_impl->expansionDepth = 0;
                if (m_impl->typeahead.empty()
                    || m_impl->typeahead.front().origin
                        == InputOrigin::Typed) {
                    m_impl->chainExpansionCount = 0;
                }
                m_impl->waiting = false;
                return ResolveStep{
                    ResolveKind::HostAction,
                    {},
                    action->action,
                    {},
                    0,
                    {}};
            }
            if (const auto *command =
                    std::get_if<CommandTarget>(
                        &full->target)) {
                m_impl->expansionDepth = 0;
                if (m_impl->typeahead.empty()
                    || m_impl->typeahead.front().origin
                        == InputOrigin::Typed) {
                    m_impl->chainExpansionCount = 0;
                }
                if (full->options.repeatPrefix) {
                    m_impl->prependRepeatPrefix(*full);
                }
                m_impl->waiting = false;
                return ResolveStep{
                    ResolveKind::Command,
                    {},
                    HostAction::None,
                    {},
                    0,
                    command->id};
            }
            m_impl->prependMapped(
                *full,
                std::get<KeyTarget>(
                    full->target).keys);
            continue;
        }
        if (m_impl->typeahead.front().origin
            == InputOrigin::RepeatPrefix) {
            // An unrelated selector exits the transient prefix, then resolves
            // the same physical key from the root. This mirrors a Vim submode
            // without swallowing input or synthesizing a second Qt event.
            while (!m_impl->typeahead.empty()
                   && m_impl->typeahead.front().origin
                       == InputOrigin::RepeatPrefix) {
                m_impl->typeahead.pop_front();
            }
            m_impl->expansionDepth = 0;
            m_impl->chainExpansionCount = 0;
            m_impl->waiting = false;
            continue;
        }
        TypeaheadEntry entry =
            m_impl->typeahead.front();
        m_impl->typeahead.pop_front();
        m_impl->expansionDepth = 0;
        if (m_impl->typeahead.empty()
            || m_impl->typeahead.front().origin
                == InputOrigin::Typed) {
            m_impl->chainExpansionCount = 0;
        }
        m_impl->waiting = false;
        return ResolveStep{
            ResolveKind::EmitKey,
            entry.key,
            HostAction::None,
            {},
            entry.inputToken,
            {}};
    }
    m_impl->expansionDepth = 0;
    m_impl->chainExpansionCount = 0;
    m_impl->waiting = false;
    return ResolveStep{
        ResolveKind::Empty,
        {},
        HostAction::None,
        {},
        0,
        {}};
}

bool InputEngine::hasMappingPrefix(
    const MappingMode mode,
    const std::optional<BufferId> buffer,
    const KeyAtom &key,
    const std::optional<WindowId> window) const
{
    if (!m_impl->typeahead.empty()) {
        return true;
    }
    const std::deque<TypeaheadEntry> probe{
        TypeaheadEntry{
            key,
            InputOrigin::Typed,
            MapPermission::Allow,
            false,
            1}};
    const LookupResult lookup =
        m_impl->lookup(
            mode, buffer, window, probe, false);
    return lookup.full.has_value() || lookup.partial;
}

bool InputEngine::mappingConsumesAfterAppending(
    const MappingMode mode,
    const std::optional<BufferId> buffer,
    const KeyAtom &key,
    MappingMode *const literalMode,
    const std::optional<WindowId> window,
    std::optional<KeyAtom> *const literalBeforeObserved,
    bool *const waitsAtDirectChild) const
{
    if (literalBeforeObserved != nullptr) {
        literalBeforeObserved->reset();
    }
    if (waitsAtDirectChild != nullptr) {
        *waitsAtDirectChild = false;
    }
    std::deque<TypeaheadEntry> probe =
        m_impl->typeahead;
    constexpr std::uint64_t observedToken =
        std::numeric_limits<std::uint64_t>::max();
    probe.push_back(
        TypeaheadEntry{
            key,
            InputOrigin::Typed,
            MapPermission::Allow,
            false,
            observedToken});
    std::size_t expansionDepth =
        m_impl->expansionDepth;
    std::size_t chainExpansionCount =
        m_impl->chainExpansionCount;
    MappingMode probeMode = mode;

    // Resolve exactly as the live engine does. Host actions and errors can be
    // produced by an earlier prefix while the observed key remains queued, so
    // keep walking until that key is consumed, emitted, or becomes partial.
    for (std::size_t iteration = 0;
         iteration < kMaximumMappingDepth * 2;
         ++iteration) {
        if (probe.empty()) {
            return true;
        }
        const LookupResult lookup =
            m_impl->lookup(
                probeMode,
                buffer,
                window,
                probe,
                false);
        const Mapping *full = nullptr;
        if (lookup.full
            && *lookup.full < m_impl->mappings.size()
            && m_impl->mappings[*lookup.full]) {
            full = &*m_impl->mappings[*lookup.full];
        }
        if (lookup.partial
            && (full == nullptr
                || !full->options.nowait)) {
            if (waitsAtDirectChild != nullptr) {
                // The complete live prefix plus the observed physical key is
                // itself a waiting trie node. No mapping action or replay has
                // run, so which-key may update this child inside the same
                // visible discovery session.
                *waitsAtDirectChild = true;
            }
            return true;
        }
        if (full != nullptr) {
            bool consumedObserved = false;
            for (std::size_t index = 0;
                 index < lookup.fullLength;
                 ++index) {
                consumedObserved =
                    consumedObserved
                    || probe.front().inputToken
                           == observedToken;
                probe.pop_front();
            }
            if (consumedObserved) {
                return true;
            }
            if (++expansionDepth >= kMaximumMappingDepth
                || ++chainExpansionCount
                    >= kMaximumMappingChainExpansions) {
                Implementation::discardGeneratedPrefixFrom(
                    probe);
                expansionDepth = 0;
                chainExpansionCount = 0;
                continue;
            }
            if (!std::holds_alternative<KeyTarget>(
                    full->target)) {
                expansionDepth = 0;
                if (probe.empty()
                    || probe.front().origin
                        == InputOrigin::Typed) {
                    chainExpansionCount = 0;
                }
                if (full->options.repeatPrefix) {
                    Implementation::prependRepeatPrefixTo(
                        probe, *full);
                }
                continue;
            }
            Implementation::prependMappedTo(
                probe,
                *full,
                std::get<KeyTarget>(full->target).keys);
            continue;
        }
        if (probe.front().origin
            == InputOrigin::RepeatPrefix) {
            while (!probe.empty()
                   && probe.front().origin
                       == InputOrigin::RepeatPrefix) {
                probe.pop_front();
            }
            expansionDepth = 0;
            chainExpansionCount = 0;
            continue;
        }
        const TypeaheadEntry emitted =
            probe.front();
        probe.pop_front();
        expansionDepth = 0;
        if (probe.empty()
            || probe.front().origin
                == InputOrigin::Typed) {
            chainExpansionCount = 0;
        }
        if (emitted.inputToken == observedToken) {
            if (literalMode != nullptr) {
                *literalMode = probeMode;
            }
            return false;
        }
        if (literalBeforeObserved != nullptr
            && !*literalBeforeObserved) {
            *literalBeforeObserved = emitted.key;
        }
        if (probeMode == MappingMode::Insert
            && isInsertModeExitKey(emitted.key)) {
            probeMode = MappingMode::Normal;
        }
    }
    // A pathological recursive chain is captured so the live resolver can
    // report maxmapdepth instead of leaking an arbitrary host shortcut.
    return true;
}

std::optional<MappingPrefixSnapshot>
InputEngine::pendingPrefixSnapshot(
    const MappingMode mode,
    const std::optional<BufferId> buffer,
    const std::uint64_t generation,
    const std::optional<WindowId> window) const
{
    return m_impl->prefixSnapshot(
        mode,
        buffer,
        window,
        generation);
}

std::optional<MappingPrefixSnapshot>
InputEngine::rootPrefixSnapshot(
    const MappingMode mode,
    const std::optional<BufferId> buffer,
    const std::uint64_t generation,
    const std::optional<WindowId> window) const
{
    return m_impl->prefixSnapshot(
        mode,
        buffer,
        window,
        generation,
        true);
}

bool InputEngine::retreatPendingPrefix()
{
    if (!m_impl->waiting
        || m_impl->typeahead.empty()) {
        return false;
    }
    m_impl->typeahead.pop_back();
    m_impl->waiting = !m_impl->typeahead.empty();
    if (m_impl->typeahead.empty()) {
        m_impl->expansionDepth = 0;
        m_impl->chainExpansionCount = 0;
    }
    return true;
}

bool InputEngine::empty() const noexcept
{
    return m_impl->typeahead.empty();
}

bool InputEngine::hasPendingTypeahead() const noexcept
{
    return m_impl->waiting
        && !m_impl->typeahead.empty();
}

void InputEngine::clear()
{
    m_impl->typeahead.clear();
    m_impl->expansionDepth = 0;
    m_impl->chainExpansionCount = 0;
    m_impl->waiting = false;
}

} // namespace vkui::vk::detail
