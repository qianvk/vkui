#include "VkCanonicalKeyEvent.h"

#include <QChar>
#include <QKeyEvent>
#include <QString>

#include <optional>
#include <utility>

namespace vkui::vk::detail {
namespace {

[[nodiscard]] KeyModifier semanticModifiers(
    const QKeyEvent &event) noexcept
{
    KeyModifier modifiers = KeyModifier::None;
#ifdef Q_OS_MACOS
    // Qt exposes Command as ControlModifier and physical Control as
    // MetaModifier on macOS. VK uses semantic Vim-Control and Super.
    if (event.modifiers().testFlag(Qt::MetaModifier)) {
        modifiers = modifiers | KeyModifier::Control;
    }
    if (event.modifiers().testFlag(Qt::ControlModifier)) {
        modifiers = modifiers | KeyModifier::Super;
    }
#else
    if (event.modifiers().testFlag(Qt::ControlModifier)) {
        modifiers = modifiers | KeyModifier::Control;
    }
    if (event.modifiers().testFlag(Qt::MetaModifier)) {
        modifiers = modifiers | KeyModifier::Super;
    }
#endif
    if (event.modifiers().testFlag(Qt::AltModifier)) {
        modifiers = modifiers | KeyModifier::Alt;
    }
    if (event.modifiers().testFlag(Qt::ShiftModifier)) {
        modifiers = modifiers | KeyModifier::Shift;
    }
    return modifiers;
}

[[nodiscard]] std::optional<SpecialKey> specialFromQt(
    const int key) noexcept
{
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
        return static_cast<SpecialKey>(
            static_cast<int>(SpecialKey::F1)
            + key - Qt::Key_F1);
    }
    switch (key) {
    case Qt::Key_Escape:
        return SpecialKey::Escape;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return SpecialKey::Enter;
    case Qt::Key_Backspace:
        return SpecialKey::Backspace;
    case Qt::Key_Delete:
        return SpecialKey::Delete;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return SpecialKey::Tab;
    case Qt::Key_Left:
        return SpecialKey::Left;
    case Qt::Key_Right:
        return SpecialKey::Right;
    case Qt::Key_Up:
        return SpecialKey::Up;
    case Qt::Key_Down:
        return SpecialKey::Down;
    case Qt::Key_Home:
        return SpecialKey::Home;
    case Qt::Key_End:
        return SpecialKey::End;
    case Qt::Key_PageUp:
        return SpecialKey::PageUp;
    case Qt::Key_PageDown:
        return SpecialKey::PageDown;
    case Qt::Key_Insert:
        return SpecialKey::Insert;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<char32_t> fallbackCharacter(
    const int key,
    const KeyModifier modifiers) noexcept
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        char32_t value =
            U'a' + static_cast<char32_t>(key - Qt::Key_A);
        if (hasModifier(modifiers, KeyModifier::Shift)
            && !hasModifier(
                modifiers,
                KeyModifier::Control)) {
            value -= U'a' - U'A';
        }
        return value;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        // Vim names ASCII 0x1e as CTRL-^, while desktop toolkits commonly
        // report the physical key as CTRL-6. Canonicalize that one semantic
        // key here so native commands and user mappings written as <C-^>
        // observe the same atom on every platform.
        if (key == Qt::Key_6
            && hasModifier(modifiers, KeyModifier::Control)) {
            return U'^';
        }
        return U'0' + static_cast<char32_t>(key - Qt::Key_0);
    }
    switch (key) {
    case Qt::Key_Space: return U' ';
    case Qt::Key_BracketLeft: return U'[';
    case Qt::Key_BracketRight: return U']';
    case Qt::Key_Backslash: return U'\\';
    case Qt::Key_Semicolon: return U';';
    case Qt::Key_Apostrophe: return U'\'';
    case Qt::Key_Comma: return U',';
    case Qt::Key_Period: return U'.';
    case Qt::Key_Slash: return U'/';
    case Qt::Key_Minus: return U'-';
    case Qt::Key_Equal: return U'=';
    case Qt::Key_Plus: return U'+';
    case Qt::Key_Less: return U'<';
    case Qt::Key_Greater: return U'>';
    case Qt::Key_Bar: return U'|';
    case Qt::Key_Underscore: return U'_';
    case Qt::Key_AsciiCircum: return U'^';
    case Qt::Key_QuoteLeft: return U'`';
    default: return std::nullopt;
    }
}

void canonicalize(
    const QKeyEvent &event,
    KeySequence &result)
{
    result.clear();
    KeyModifier modifiers = semanticModifiers(event);
    if (const auto special = specialFromQt(event.key())) {
        if (event.key() == Qt::Key_Backtab) {
            modifiers = modifiers | KeyModifier::Shift;
        }
        result.push_back(
            specialKey(*special, modifiers));
        return;
    }

    const bool textIsLiteral =
        !hasModifier(modifiers, KeyModifier::Control)
        && !hasModifier(modifiers, KeyModifier::Alt)
        && !hasModifier(modifiers, KeyModifier::Super)
        && !event.text().isEmpty();
    if (textIsLiteral) {
        const QString &text = event.text();
        if (result.capacity()
            < static_cast<std::size_t>(text.size())) {
            result.reserve(
                static_cast<std::size_t>(text.size()));
        }
        for (qsizetype index = 0;
             index < text.size();
             ++index) {
            char32_t scalar =
                static_cast<char32_t>(
                    text.at(index).unicode());
            if (QChar::isHighSurrogate(
                    static_cast<char16_t>(scalar))
                && index + 1 < text.size()
                && QChar::isLowSurrogate(
                    text.at(index + 1).unicode())) {
                scalar = static_cast<char32_t>(
                    QChar::surrogateToUcs4(
                        text.at(index),
                        text.at(index + 1)));
                ++index;
            }
            // Shift is encoded in the resulting Unicode scalar.
            result.push_back(characterKey(scalar));
        }
        return;
    }

    const auto fallback =
        fallbackCharacter(event.key(), modifiers);
    if (!fallback) {
        return;
    }
    if (*fallback == U'^'
        && hasModifier(modifiers, KeyModifier::Control)) {
        // Shift is part of producing '^' on many keyboard layouts, not a
        // second Vim modifier. CTRL-6, CTRL-SHIFT-6 and CTRL-^ are one key.
        modifiers = modifiers & ~KeyModifier::Shift;
    }
    if (modifiers == KeyModifier::Shift
        && *fallback >= U'A'
        && *fallback <= U'Z') {
        modifiers = KeyModifier::None;
    }
    result.push_back(
        characterKey(*fallback, modifiers));
}

} // namespace

class CanonicalKeyEventCache::Implementation final
{
public:
    Implementation()
    {
        // Most physical keys canonicalize to one atom. A small retained
        // capacity also covers common composed/text events without allocating
        // again when the last-value signature changes.
        keys.reserve(4);
    }

    struct Signature final
    {
        int key = 0;
        Qt::KeyboardModifiers modifiers = Qt::NoModifier;
        QString text;
        quint64 nativeScanCode = 0;
        quint64 nativeVirtualKey = 0;
        quint64 nativeModifiers = 0;
        bool autoRepeat = false;
        int count = 0;

        [[nodiscard]] bool matches(
            const QKeyEvent &event) const noexcept
        {
            return key == event.key()
                && modifiers == event.modifiers()
                && text == event.text()
                && nativeScanCode
                    == event.nativeScanCode()
                && nativeVirtualKey
                    == event.nativeVirtualKey()
                && nativeModifiers
                    == event.nativeModifiers()
                && autoRepeat == event.isAutoRepeat()
                && count == event.count();
        }

        static Signature from(
            const QKeyEvent &event)
        {
            return Signature{
                event.key(),
                event.modifiers(),
                event.text(),
                event.nativeScanCode(),
                event.nativeVirtualKey(),
                event.nativeModifiers(),
                event.isAutoRepeat(),
                event.count()};
        }
    };

    mutable std::optional<Signature> signature;
    mutable KeySequence keys;
    mutable std::size_t hits = 0;
    mutable std::size_t misses = 0;
};

CanonicalKeyEventCache::CanonicalKeyEventCache()
    : m_impl(std::make_unique<Implementation>())
{
}

CanonicalKeyEventCache::~CanonicalKeyEventCache() = default;

const KeySequence &CanonicalKeyEventCache::keys(
    const QKeyEvent &event) const
{
    if (m_impl->signature
        && m_impl->signature->matches(event)) {
        ++m_impl->hits;
        return m_impl->keys;
    }
    m_impl->signature =
        Implementation::Signature::from(event);
    canonicalize(event, m_impl->keys);
    ++m_impl->misses;
    return m_impl->keys;
}

std::size_t CanonicalKeyEventCache::hitCount() const noexcept
{
    return m_impl->hits;
}

std::size_t CanonicalKeyEventCache::missCount() const noexcept
{
    return m_impl->misses;
}

} // namespace vkui::vk::detail
