#include "VkCoreInternal.h"

namespace vkui::vk::core_detail {

[[nodiscard]] std::vector<std::size_t> lineStarts(
    const std::u16string &text)
{
    std::vector<std::size_t> starts{0};
    starts.reserve(
        1 + static_cast<std::size_t>(
                std::count(text.cbegin(), text.cend(), u'\n')));
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == u'\n') {
            starts.push_back(index + 1);
        }
    }
    return starts;
}

[[nodiscard]] bool plainCharacter(
    const KeyAtom &key,
    const char32_t expected) noexcept
{
    return key.modifiers == KeyModifier::None
        && detail::character(key)
               == std::optional<char32_t>(expected);
}

[[nodiscard]] bool modifiedCharacter(
    const KeyAtom &key,
    const char32_t expected,
    const KeyModifier modifier) noexcept
{
    return key.modifiers == modifier
        && detail::character(key)
               == std::optional<char32_t>(expected);
}

[[nodiscard]] bool isCoreHandledInsertKey(
    const KeyAtom &key) noexcept
{
    if (key.modifiers == KeyModifier::None) {
        if (detail::character(key)) {
            return true;
        }
        return detail::isSpecialKey(key, SpecialKey::Escape)
            || detail::isSpecialKey(key, SpecialKey::Enter)
            || detail::isSpecialKey(key, SpecialKey::Tab)
            || detail::isSpecialKey(key, SpecialKey::Backspace)
            || detail::isSpecialKey(key, SpecialKey::Delete)
            || detail::isSpecialKey(key, SpecialKey::Left)
            || detail::isSpecialKey(key, SpecialKey::Right)
            || detail::isSpecialKey(key, SpecialKey::Up)
            || detail::isSpecialKey(key, SpecialKey::Down)
            || detail::isSpecialKey(key, SpecialKey::PageUp)
            || detail::isSpecialKey(key, SpecialKey::PageDown)
            || detail::isSpecialKey(key, SpecialKey::Home)
            || detail::isSpecialKey(key, SpecialKey::End);
    }
    if (modifiedCharacter(
            key, U'c', KeyModifier::Control)
        || modifiedCharacter(
            key, U'[', KeyModifier::Control)
        || modifiedCharacter(
            key, U'n', KeyModifier::Control)
        || modifiedCharacter(
            key, U'p', KeyModifier::Control)) {
        return true;
    }
    const auto scalar = detail::character(key);
    return key.modifiers == KeyModifier::Super
        && scalar
        && (*scalar == U'a'
            || *scalar == U'c'
            || *scalar == U'v'
            || *scalar == U'x'
            || *scalar == U'y'
            || *scalar == U'z');
}

[[nodiscard]] bool isInsertNavigationKey(
    const KeyAtom &key) noexcept
{
    return key.modifiers == KeyModifier::None
        && (detail::isSpecialKey(key, SpecialKey::Left)
            || detail::isSpecialKey(key, SpecialKey::Right)
            || detail::isSpecialKey(key, SpecialKey::Up)
            || detail::isSpecialKey(key, SpecialKey::Down)
            || detail::isSpecialKey(key, SpecialKey::PageUp)
            || detail::isSpecialKey(key, SpecialKey::PageDown)
            || detail::isSpecialKey(key, SpecialKey::Home)
            || detail::isSpecialKey(key, SpecialKey::End));
}

[[nodiscard]] bool isInsertKeywordCompletionKey(
    const KeyAtom &key) noexcept
{
    return modifiedCharacter(
               key, U'n', KeyModifier::Control)
        || modifiedCharacter(
               key, U'p', KeyModifier::Control);
}

[[nodiscard]] MappingMode mappingModeFor(
    const Mode mode) noexcept
{
    switch (mode) {
    case Mode::Insert:
    case Mode::Replace:
        return MappingMode::Insert;
    case Mode::Visual:
        return MappingMode::Visual;
    case Mode::OperatorPending:
        return MappingMode::OperatorPending;
    case Mode::Normal:
        return MappingMode::Normal;
    }
    return MappingMode::Normal;
}

void appendUtf16(
    std::u16string &target,
    const char32_t scalar)
{
    if (scalar <= 0xffffU) {
        target.push_back(static_cast<char16_t>(scalar));
        return;
    }
    const char32_t value = scalar - 0x10000U;
    target.push_back(static_cast<char16_t>(
        0xd800U + (value >> 10U)));
    target.push_back(static_cast<char16_t>(
        0xdc00U + (value & 0x3ffU)));
}

[[nodiscard]] detail::KeySequence macroKeysFromText(
    const std::u16string_view text)
{
    detail::KeySequence keys;
    keys.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char16_t first = text[index];
        char32_t scalar = first;
        if (QChar::isHighSurrogate(first)
            && index + 1 < text.size()
            && QChar::isLowSurrogate(text[index + 1])) {
            scalar = QChar::surrogateToUcs4(
                first, text[++index]);
        }
        if (scalar == U'\n' || scalar == U'\r') {
            keys.push_back(detail::specialKey(SpecialKey::Enter));
        } else if (scalar == U'\t') {
            keys.push_back(detail::specialKey(SpecialKey::Tab));
        } else if (scalar == U'\x1b') {
            keys.push_back(detail::specialKey(SpecialKey::Escape));
        } else {
            keys.push_back(detail::characterKey(scalar));
        }
    }
    return keys;
}

void appendRecordedKeyText(
    std::u16string &text,
    const KeyAtom &key)
{
    if (const auto scalar = detail::character(key)) {
        if (key.modifiers == KeyModifier::Control
            && *scalar >= U'@' && *scalar <= U'_') {
            text.push_back(static_cast<char16_t>(*scalar - U'@'));
        } else if (key.modifiers == KeyModifier::Control
                   && *scalar >= U'a' && *scalar <= U'z') {
            text.push_back(static_cast<char16_t>(*scalar - U'a' + 1));
        } else {
            appendUtf16(text, *scalar);
        }
        return;
    }
    if (detail::isSpecialKey(key, SpecialKey::Enter)) {
        text.push_back(u'\n');
    } else if (detail::isSpecialKey(key, SpecialKey::Tab)) {
        text.push_back(u'\t');
    } else if (detail::isSpecialKey(key, SpecialKey::Escape)) {
        text.push_back(u'\x1b');
    } else if (detail::isSpecialKey(key, SpecialKey::Backspace)) {
        text.push_back(u'\b');
    } else if (detail::isSpecialKey(key, SpecialKey::Delete)) {
        text.push_back(u'\x7f');
    }
}

[[nodiscard]] bool pathSeparator(
    const char value) noexcept
{
    return value == '/' || value == '\\';
}

[[nodiscard]] std::string_view normalizedPathRoot(
    std::string_view root) noexcept
{
    while (root.size() > 1
           && pathSeparator(root.back())
           && !(root.size() == 3
                && root[1] == ':')) {
        root.remove_suffix(1);
    }
    return root;
}

[[nodiscard]] bool pathAtOrBelow(
    const std::string_view path,
    const std::string_view rawRoot) noexcept
{
    const std::string_view root =
        normalizedPathRoot(rawRoot);
    if (root.empty()
        || path.size() < root.size()
        || !path.starts_with(root)) {
        return false;
    }
    return path.size() == root.size()
        || pathSeparator(root.back())
        || pathSeparator(path[root.size()]);
}

} // namespace vkui::vk::core_detail
