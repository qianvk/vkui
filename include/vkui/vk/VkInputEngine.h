#pragma once

#include "VkTypes.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vkui::vk::detail {

enum class KeyModifier : std::uint8_t
{
    None = 0,
    Shift = 1U << 0U,
    Control = 1U << 1U,
    Alt = 1U << 2U,
    Super = 1U << 3U
};

[[nodiscard]] constexpr KeyModifier operator|(
    const KeyModifier lhs,
    const KeyModifier rhs) noexcept
{
    return static_cast<KeyModifier>(
        static_cast<std::uint8_t>(lhs)
        | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr KeyModifier operator&(
    const KeyModifier lhs,
    const KeyModifier rhs) noexcept
{
    return static_cast<KeyModifier>(
        static_cast<std::uint8_t>(lhs)
        & static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr KeyModifier operator~(
    const KeyModifier value) noexcept
{
    return static_cast<KeyModifier>(
        ~static_cast<std::uint8_t>(value));
}

[[nodiscard]] constexpr bool hasModifier(
    const KeyModifier value,
    const KeyModifier flag) noexcept
{
    return (value & flag) != KeyModifier::None;
}

enum class SpecialKey : std::uint16_t
{
    Escape,
    Enter,
    Backspace,
    Delete,
    Tab,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    Insert,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,
    F25,
    F26,
    F27,
    F28,
    F29,
    F30,
    F31,
    F32,
    F33,
    F34,
    F35
};

struct KeyAtom final
{
    // Unicode scalars occupy their natural range. Special keys start above it.
    std::uint32_t code = 0;
    KeyModifier modifiers = KeyModifier::None;

    friend bool operator==(const KeyAtom &,
                           const KeyAtom &) = default;
};

using KeySequence = std::vector<KeyAtom>;

[[nodiscard]] KeyAtom characterKey(
    char32_t value,
    KeyModifier modifiers = KeyModifier::None) noexcept;
[[nodiscard]] KeyAtom specialKey(
    SpecialKey value,
    KeyModifier modifiers = KeyModifier::None) noexcept;
[[nodiscard]] bool isSpecialKey(
    const KeyAtom &key,
    SpecialKey value) noexcept;
[[nodiscard]] std::optional<char32_t> character(
    const KeyAtom &key) noexcept;

enum class InputOrigin : std::uint8_t
{
    Typed,
    Mapping,
    RepeatPrefix,
    Macro
};

enum class MapPermission : std::uint8_t
{
    Allow,
    Deny,
    ScriptOnly
};

struct TypeaheadEntry final
{
    KeyAtom key;
    InputOrigin origin = InputOrigin::Typed;
    MapPermission permission = MapPermission::Allow;
    bool silent = false;
    // Non-zero only for a physical key appended at the ingress boundary.
    // Mapping RHS entries intentionally do not inherit the token: this lets
    // dry-run resolution tell whether a specific host key was consumed by a
    // mapping or survived as a literal key.
    std::uint64_t inputToken = 0;
};

enum class ResolveKind : std::uint8_t
{
    Empty,
    EmitKey,
    HostAction,
    NeedMore,
    Error,
    Command
};

struct ResolveStep final
{
    ResolveKind kind = ResolveKind::Empty;
    KeyAtom key;
    HostAction action = HostAction::None;
    std::string message;
    std::uint64_t inputToken = 0;
    // Set only when kind is Command.
    std::string commandId{};
    InputHintWaitPolicy waitPolicy =
        InputHintWaitPolicy::TimedMapping;
};

struct ParseResult final
{
    KeySequence keys;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error.empty();
    }
};

/**
 * Neovim-style typeahead and mapping resolver.
 *
 * Mappings are definition-time objects. Runtime input is always represented
 * by typed key atoms carrying per-key remap metadata; notation such as
 * <Leader> never enters the hot input path.
 */
class InputEngine final
{
public:
    InputEngine();
    ~InputEngine();

    InputEngine(const InputEngine &) = delete;
    InputEngine &operator=(const InputEngine &) = delete;

    void setLeader(KeySequence leader);
    void setLocalLeader(KeySequence leader);
    [[nodiscard]] ParseResult parse(
        std::u32string_view notation) const;

    [[nodiscard]] MappingId addMapping(
        const MappingDefinition &definition,
        std::string *error = nullptr);
    /**
     * Validates and publishes one owner activation batch atomically.
     *
     * Definitions preserve addMapping() order/replacement semantics. A
     * rejected member leaves every prior mapping and the id sequence
     * untouched; a successful batch rebuilds resolver tries exactly once.
     */
    [[nodiscard]] std::vector<MappingId> addMappings(
        std::span<const MappingDefinition> definitions,
        std::string *error = nullptr);
    [[nodiscard]] bool removeMapping(MappingId id);
    /**
     * Removes one owner transaction's mappings with a single trie rebuild.
     */
    [[nodiscard]] std::size_t removeMappings(
        std::span<const MappingId> ids);
    /**
     * Returns true only when at least one id participates in the resolver's
     * currently waiting prefix for the supplied runtime context.
     *
     * This query is intentionally evaluated before removal. It lets VkCore
     * invalidate affected typeahead instead of replaying a revoked prefix as
     * literal Normal-mode input, while leaving unrelated plugin prefixes
     * untouched.
     */
    [[nodiscard]] bool pendingResolutionUsesAnyMapping(
        std::span<const MappingId> ids,
        MappingMode mode,
        std::optional<BufferId> buffer,
        std::optional<WindowId> window = std::nullopt) const;
    /**
     * Removes every mapping scoped to a destroyed window.
     *
     * Window ids are process-local capabilities. Keeping their mappings
     * after the owning window disappears could make a recycled id inherit
     * stale plugin behavior.
     */
    [[nodiscard]] std::size_t removeWindowMappings(
        WindowId window);
    /** Removes all mappings scoped to a deleted buffer in one rebuild. */
    [[nodiscard]] std::size_t removeBufferMappings(
        BufferId buffer);
    /**
     * Removes mappings for a destroyed buffer batch with one scan and at
     * most one resolver rebuild.
     */
    [[nodiscard]] std::size_t removeBufferMappings(
        std::span<const BufferId> buffers);

    void appendTyped(const KeyAtom &key);
    /**
     * Appends a physical key that must bypass mapping lookup.
     *
     * This is used for the mismatching tail of an already-started native
     * command transaction.  For example, once a timed `<C-w>s` mapping has
     * fallen back to Vim's native `<C-w>` grammar, the following `l` must
     * complete that grammar instead of being reinterpreted as a window-local
     * `l` mapping.
     */
    void appendUnmapped(const KeyAtom &key);
    /**
     * Prepends replayed macro keys ahead of the remaining typeahead.
     *
     * Macro entries remain remappable like physical input but carry no
     * physical input token. Prepending preserves nested macro call order.
     */
    void prependMacro(
        const KeySequence &keys,
        std::size_t count = 1);
    [[nodiscard]] ResolveStep resolve(
        MappingMode mode,
        std::optional<BufferId> buffer,
        bool timedOut,
        std::optional<WindowId> window = std::nullopt);
    [[nodiscard]] bool hasMappingPrefix(
        MappingMode mode,
        std::optional<BufferId> buffer,
        const KeyAtom &key,
        std::optional<WindowId> window = std::nullopt) const;
    /**
     * Runs the real resolver against a private typeahead snapshot.
     *
     * This is deliberately not a one-level prefix lookup: recursive mappings
     * can transform an earlier pending key and then consume the appended key.
     * waitsAtDirectChild is true only when the unchanged live prefix plus the
     * observed key is itself a waiting trie node; executing a mapping or its
     * replay can never set it.
     */
    [[nodiscard]] bool mappingConsumesAfterAppending(
        MappingMode mode,
        std::optional<BufferId> buffer,
        const KeyAtom &key,
        MappingMode *literalMode = nullptr,
        std::optional<WindowId> window = std::nullopt,
        std::optional<KeyAtom> *literalBeforeObserved = nullptr,
        bool *waitsAtDirectChild = nullptr) const;

    /**
     * Enumerates the live resolver node reached by the pending typeahead.
     *
     * Lookup is O(prefix length + direct child count). Buffer-local and global
     * nodes are merged with local precedence and one candidate per next key;
     * no mapping-table scan occurs on the input hot path.
     */
    [[nodiscard]] std::optional<MappingPrefixSnapshot>
    pendingPrefixSnapshot(
        MappingMode mode,
        std::optional<BufferId> buffer,
        std::uint64_t generation,
        std::optional<WindowId> window = std::nullopt) const;

    /**
     * Enumerates direct mappings at the mode root without creating input.
     *
     * which-key uses this after Backspace leaves the first trigger node. The
     * returned value is presentation only: the resolver remains empty until
     * the next physical key arrives through appendTyped().
     */
    [[nodiscard]] std::optional<MappingPrefixSnapshot>
    rootPrefixSnapshot(
        MappingMode mode,
        std::optional<BufferId> buffer,
        std::uint64_t generation,
        std::optional<WindowId> window = std::nullopt) const;

    /** Removes the newest key from a currently waiting mapping prefix. */
    [[nodiscard]] bool retreatPendingPrefix();

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool hasPendingTypeahead() const noexcept;
    void clear();

private:
    class Implementation;
    std::unique_ptr<Implementation> m_impl;
};

} // namespace vkui::vk::detail
