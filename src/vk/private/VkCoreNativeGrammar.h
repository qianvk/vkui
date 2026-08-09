#pragma once

#include "VkCoreInternal.h"

namespace vkui::vk::core_detail {

struct NormalCommandDescriptor final
{
    char32_t character = U'\0';
    KeyModifier modifiers = KeyModifier::None;
    Command command = Command::MoveLeft;
};

// Simple Normal commands use data-driven dispatch. Prefixes and operators
// remain stateful grammar and can migrate into richer descriptors separately.
constexpr std::array kNormalCommands{
    NormalCommandDescriptor{
        U'h', KeyModifier::None, Command::MoveLeft},
    NormalCommandDescriptor{
        U'h', KeyModifier::Control, Command::MoveLeft},
    NormalCommandDescriptor{
        U'j', KeyModifier::None, Command::MoveDown},
    NormalCommandDescriptor{
        U'j', KeyModifier::Control, Command::MoveDown},
    // Neovim's Normal grammar treats CTRL-N/CTRL-P as exact aliases for
    // j/k.  Keep these in the native descriptor table (rather than a default
    // mapping) so counts, operators, Navigation views, and user remaps all
    // follow the same precedence rules as the canonical motions.
    NormalCommandDescriptor{
        U'n', KeyModifier::Control, Command::MoveDown},
    NormalCommandDescriptor{
        U'k', KeyModifier::None, Command::MoveUp},
    NormalCommandDescriptor{
        U'p', KeyModifier::Control, Command::MoveUp},
    NormalCommandDescriptor{
        U'a', KeyModifier::Control, Command::AddNumber},
    NormalCommandDescriptor{
        U'x', KeyModifier::Control, Command::SubtractNumber},
    NormalCommandDescriptor{
        U'g', KeyModifier::Control, Command::ShowBufferStatus},
    NormalCommandDescriptor{
        U'l', KeyModifier::None, Command::MoveRight},
    NormalCommandDescriptor{
        U' ', KeyModifier::None, Command::MoveRight},
    NormalCommandDescriptor{
        U'w', KeyModifier::None, Command::WordForward},
    NormalCommandDescriptor{
        U'b', KeyModifier::None, Command::WordBackward},
    NormalCommandDescriptor{
        U'e', KeyModifier::None, Command::WordEnd},
    NormalCommandDescriptor{
        U'W', KeyModifier::None, Command::BigWordForward},
    NormalCommandDescriptor{
        U'B', KeyModifier::None, Command::BigWordBackward},
    NormalCommandDescriptor{
        U'E', KeyModifier::None, Command::BigWordEnd},
    NormalCommandDescriptor{
        U'(', KeyModifier::None, Command::SentenceBackward},
    NormalCommandDescriptor{
        U')', KeyModifier::None, Command::SentenceForward},
    NormalCommandDescriptor{
        U'{', KeyModifier::None, Command::ParagraphBackward},
    NormalCommandDescriptor{
        U'}', KeyModifier::None, Command::ParagraphForward},
    NormalCommandDescriptor{
        U'0', KeyModifier::None, Command::LineStart},
    NormalCommandDescriptor{
        U'^', KeyModifier::None, Command::FirstNonBlank},
    NormalCommandDescriptor{
        U'$', KeyModifier::None, Command::LineEnd},
    NormalCommandDescriptor{
        U'+', KeyModifier::None,
        Command::LineDownFirstNonBlank},
    NormalCommandDescriptor{
        U'-', KeyModifier::None,
        Command::LineUpFirstNonBlank},
    NormalCommandDescriptor{
        U'm', KeyModifier::Control,
        Command::LineDownFirstNonBlank},
    NormalCommandDescriptor{
        U'_', KeyModifier::None,
        Command::CurrentLineFirstNonBlank},
    NormalCommandDescriptor{
        U'|', KeyModifier::None, Command::ScreenColumn},
    NormalCommandDescriptor{
        U'G', KeyModifier::None, Command::LastLine},
    NormalCommandDescriptor{
        U'H', KeyModifier::None, Command::WindowTop},
    NormalCommandDescriptor{
        U'M', KeyModifier::None, Command::WindowMiddle},
    NormalCommandDescriptor{
        U'L', KeyModifier::None, Command::WindowBottom},
    NormalCommandDescriptor{
        U'%', KeyModifier::None, Command::MatchPair},
    NormalCommandDescriptor{
        U'f', KeyModifier::None, Command::BeginFindForward},
    NormalCommandDescriptor{
        U'F', KeyModifier::None, Command::BeginFindBackward},
    NormalCommandDescriptor{
        U't', KeyModifier::None, Command::BeginTillForward},
    NormalCommandDescriptor{
        U'T', KeyModifier::None, Command::BeginTillBackward},
    NormalCommandDescriptor{
        U';', KeyModifier::None, Command::RepeatCharacterSearch},
    NormalCommandDescriptor{
        U',', KeyModifier::None,
        Command::RepeatCharacterSearchOpposite},
    NormalCommandDescriptor{
        U'i', KeyModifier::None, Command::EnterInsert},
    NormalCommandDescriptor{
        U'a', KeyModifier::None, Command::AppendInsert},
    NormalCommandDescriptor{
        U'I', KeyModifier::None, Command::InsertAtLineStart},
    NormalCommandDescriptor{
        U'A', KeyModifier::None, Command::AppendAtLineEnd},
    NormalCommandDescriptor{
        U'R', KeyModifier::None, Command::EnterReplaceMode},
    NormalCommandDescriptor{
        U'v', KeyModifier::None, Command::EnterVisual},
    NormalCommandDescriptor{
        U'V', KeyModifier::None, Command::EnterVisualLine},
    NormalCommandDescriptor{
        U'v', KeyModifier::Control, Command::EnterVisualBlock},
    NormalCommandDescriptor{
        U'x', KeyModifier::None, Command::DeleteUnderCursor},
    NormalCommandDescriptor{
        U'X', KeyModifier::None, Command::DeleteBeforeCursor},
    NormalCommandDescriptor{
        U'p', KeyModifier::None, Command::PutAfter},
    NormalCommandDescriptor{
        U'P', KeyModifier::None, Command::PutBefore},
    NormalCommandDescriptor{
        U'o', KeyModifier::None, Command::OpenBelow},
    NormalCommandDescriptor{
        U'O', KeyModifier::None, Command::OpenAbove},
    NormalCommandDescriptor{
        U'r', KeyModifier::None, Command::BeginReplace},
    NormalCommandDescriptor{
        U's', KeyModifier::None, Command::SubstituteCharacters},
    NormalCommandDescriptor{
        U'S', KeyModifier::None, Command::SubstituteLines},
    NormalCommandDescriptor{
        U'D', KeyModifier::None, Command::DeleteToLineEnd},
    NormalCommandDescriptor{
        U'C', KeyModifier::None, Command::ChangeToLineEnd},
    NormalCommandDescriptor{
        U'J', KeyModifier::None, Command::JoinLines},
    NormalCommandDescriptor{
        U'~', KeyModifier::None, Command::ToggleCase},
    NormalCommandDescriptor{
        U'.', KeyModifier::None, Command::RepeatLastChange},
    NormalCommandDescriptor{
        U'm', KeyModifier::None, Command::BeginSetMark},
    NormalCommandDescriptor{
        U'\'', KeyModifier::None, Command::BeginJumpMarkLine},
    NormalCommandDescriptor{
        U'`', KeyModifier::None, Command::BeginJumpMarkExact},
    NormalCommandDescriptor{
        U'o', KeyModifier::Control, Command::JumpListBack},
    NormalCommandDescriptor{
        U'i', KeyModifier::Control, Command::JumpListForward},
    NormalCommandDescriptor{
        U'^', KeyModifier::Control, Command::AlternateBuffer},
    NormalCommandDescriptor{
        U':', KeyModifier::None, Command::BeginExCommand},
    NormalCommandDescriptor{
        U'/', KeyModifier::None, Command::BeginSearchForward},
    NormalCommandDescriptor{
        U'?', KeyModifier::None, Command::BeginSearchBackward},
    NormalCommandDescriptor{
        U'n', KeyModifier::None, Command::RepeatSearch},
    NormalCommandDescriptor{
        U'N', KeyModifier::None, Command::RepeatSearchOpposite},
    NormalCommandDescriptor{
        U'*', KeyModifier::None, Command::SearchWordForwardExact},
    NormalCommandDescriptor{
        U'#', KeyModifier::None, Command::SearchWordBackwardExact},
    NormalCommandDescriptor{
        U'q', KeyModifier::None, Command::BeginMacroRecord},
    NormalCommandDescriptor{
        U'@', KeyModifier::None, Command::BeginMacroExecute},
    NormalCommandDescriptor{
        U'z', KeyModifier::None, Command::BeginViewportPosition},
    NormalCommandDescriptor{
        U'"', KeyModifier::None, Command::BeginRegisterSelect},
    NormalCommandDescriptor{
        U'u', KeyModifier::None, Command::Undo},
    NormalCommandDescriptor{
        U'r', KeyModifier::Control, Command::Redo},
    NormalCommandDescriptor{
        U'd', KeyModifier::Control, Command::ScrollHalfDown},
    NormalCommandDescriptor{
        U'u', KeyModifier::Control, Command::ScrollHalfUp},
    NormalCommandDescriptor{
        U'f', KeyModifier::Control, Command::ScrollPageDown},
    NormalCommandDescriptor{
        U'b', KeyModifier::Control, Command::ScrollPageUp},
    NormalCommandDescriptor{
        U'e', KeyModifier::Control, Command::ScrollLineDown},
    NormalCommandDescriptor{
        U'y', KeyModifier::Control, Command::ScrollLineUp},
};

[[nodiscard]] inline std::optional<Command> normalCommand(
    const KeyAtom &key) noexcept
{
    const auto character = detail::character(key);
    if (!character) {
        return std::nullopt;
    }
    const auto found = std::ranges::find_if(
        kNormalCommands,
        [&key, &character](
            const NormalCommandDescriptor &descriptor) {
            return descriptor.character == *character
                && descriptor.modifiers == key.modifiers;
        });
    return found == kNormalCommands.cend()
        ? std::nullopt
        : std::optional<Command>(found->command);
}

struct NativeCommandDescriptor final
{
    NativeHintDescriptor hint;
    Command command = Command::MoveLeft;
};

struct NativeSemanticCommandDescriptor final
{
    NativeHintDescriptor hint;
    std::string_view commandId;
};

struct NativeWindowAliasDescriptor final
{
    NativeKeyDescriptor key;
    WindowOperation operation = WindowOperation::FocusLeft;
};

struct NativeRegisterDescriptor final
{
    NativeHintDescriptor hint;
};

struct NativeReplacementDescriptor final
{
    NativeHintDescriptor hint;
    /**
     * AnyCharacter descriptors derive the replacement from the typed key.
     * Special-key descriptors carry their normalized buffer character here.
     */
    char32_t replacement = U'\0';
};

struct NativeOperatorDescriptor final
{
    NativeHintDescriptor hint;
    OperatorKind kind = OperatorKind::Delete;
};

struct NativePrefixDescriptor final
{
    NativeKeyDescriptor key;
    CommandPrefix prefix = CommandPrefix::None;
};

enum class OperatorContinuationKind : std::uint8_t
{
    Motion,
    GotoPrefix,
    BracketLeftPrefix,
    BracketRightPrefix,
    CharacterSearch,
    RepeatCharacterSearch,
    MatchingPair,
    TextObjectPrefix
};

struct NativeOperatorContinuationDescriptor final
{
    NativeHintDescriptor hint;
    OperatorContinuationKind kind =
        OperatorContinuationKind::Motion;
    Command command = Command::MoveLeft;
};

[[nodiscard]] constexpr NativeKeyDescriptor characterKey(
    const char32_t character,
    const std::u32string_view notation,
    const KeyModifier modifiers = KeyModifier::None) noexcept
{
    return NativeKeyDescriptor{
        NativeKeyKind::Character,
        character,
        SpecialKey::Escape,
        modifiers,
        notation};
}

[[nodiscard]] constexpr NativeKeyDescriptor specialKey(
    const SpecialKey special,
    const std::u32string_view notation) noexcept
{
    return NativeKeyDescriptor{
        NativeKeyKind::Special,
        U'\0',
        special,
        KeyModifier::None,
        notation};
}

[[nodiscard]] constexpr NativeKeyDescriptor decimalDigitKey(
    const std::u32string_view notation) noexcept
{
    return NativeKeyDescriptor{
        NativeKeyKind::DecimalDigit,
        U'\0',
        SpecialKey::Escape,
        KeyModifier::None,
        notation};
}

[[nodiscard]] constexpr NativeKeyDescriptor anyCharacterKey(
    const std::u32string_view notation) noexcept
{
    return NativeKeyDescriptor{
        NativeKeyKind::AnyCharacter,
        U'\0',
        SpecialKey::Escape,
        KeyModifier::None,
        notation};
}

[[nodiscard]] constexpr NativeKeyDescriptor lowercaseLetterKey(
    const std::u32string_view notation) noexcept
{
    return NativeKeyDescriptor{
        NativeKeyKind::LowercaseLetter,
        U'\0',
        SpecialKey::Escape,
        KeyModifier::None,
        notation};
}

[[nodiscard]] constexpr NativeKeyDescriptor uppercaseLetterKey(
    const std::u32string_view notation) noexcept
{
    return NativeKeyDescriptor{
        NativeKeyKind::UppercaseLetter,
        U'\0',
        SpecialKey::Escape,
        KeyModifier::None,
        notation};
}

[[nodiscard]] inline bool nativeKeyMatches(
    const NativeKeyDescriptor &descriptor,
    const KeyAtom &key) noexcept
{
    if (key.modifiers != descriptor.modifiers) {
        return false;
    }
    switch (descriptor.kind) {
    case NativeKeyKind::Character:
        return detail::character(key)
            == std::optional<char32_t>(
                descriptor.character);
    case NativeKeyKind::Special:
        return detail::isSpecialKey(
            key, descriptor.special);
    case NativeKeyKind::DecimalDigit: {
        const std::optional<char32_t> character =
            detail::character(key);
        return character
            && *character >= U'0'
            && *character <= U'9';
    }
    case NativeKeyKind::LowercaseLetter: {
        const std::optional<char32_t> character =
            detail::character(key);
        return character
            && *character >= U'a'
            && *character <= U'z';
    }
    case NativeKeyKind::UppercaseLetter: {
        const std::optional<char32_t> character =
            detail::character(key);
        return character
            && *character >= U'A'
            && *character <= U'Z';
    }
    case NativeKeyKind::AnyCharacter:
        return detail::character(key).has_value();
    }
    return false;
}

template<typename Descriptor, std::size_t Size>
[[nodiscard]] const Descriptor *findNativeDescriptor(
    const std::array<Descriptor, Size> &descriptors,
    const KeyAtom &key) noexcept
{
    const auto found = std::ranges::find_if(
        descriptors,
        [&key](const Descriptor &descriptor) {
            return nativeKeyMatches(
                descriptor.hint.key, key);
        });
    return found == descriptors.cend()
        ? nullptr
        : &*found;
}

template<std::size_t Size>
[[nodiscard]] const NativePrefixDescriptor *
findNativePrefixDescriptor(
    const std::array<NativePrefixDescriptor, Size> &descriptors,
    const KeyAtom &key) noexcept
{
    const auto found = std::ranges::find_if(
        descriptors,
        [&key](const NativePrefixDescriptor &descriptor) {
            return nativeKeyMatches(
                descriptor.key, key);
        });
    return found == descriptors.cend()
        ? nullptr
        : &*found;
}

constexpr std::array kPrefixStarters{
    NativePrefixDescriptor{
        characterKey(U'g', U"g"),
        CommandPrefix::G},
    NativePrefixDescriptor{
        characterKey(U'[', U"["),
        CommandPrefix::BracketLeft},
    NativePrefixDescriptor{
        characterKey(U']', U"]"),
        CommandPrefix::BracketRight},
    NativePrefixDescriptor{
        characterKey(
            U'w', U"<C-w>", KeyModifier::Control),
        CommandPrefix::Window},
};

constexpr std::array kGotoGrammar{
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'g', U"g"),
            "First line",
            "Goto",
            true,
            false,
            10},
        Command::FirstLine},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'j', U"j"),
            "Display row down",
            "Display motion",
            true,
            false,
            11},
        Command::DisplayRowDown},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'k', U"k"),
            "Display row up",
            "Display motion",
            true,
            false,
            12},
        Command::DisplayRowUp},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'0', U"0"),
            "Display row start",
            "Display motion",
            true,
            false,
            13},
        Command::DisplayRowStart},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'^', U"^"),
            "Display row first non-blank",
            "Display motion",
            true,
            false,
            14},
        Command::DisplayRowFirstNonBlank},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'$', U"$"),
            "Display row end",
            "Display motion",
            true,
            false,
            15},
        Command::DisplayRowEnd},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'm', U"m"),
            "Display row middle",
            "Display motion",
            true,
            false,
            16},
        Command::DisplayRowMiddle},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'M', U"M"),
            "Text line percentage",
            "Display motion",
            true,
            false,
            17},
        Command::TextMiddle},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'_', U"_"),
            "Last non-blank character",
            "Goto",
            true,
            false,
            20},
        Command::LastNonBlank},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'e', U"e"),
            "End of previous word",
            "Motion",
            true,
            false,
            21},
        Command::WordEndBackward},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'E', U"E"),
            "End of previous WORD",
            "Motion",
            true,
            false,
            22},
        Command::BigWordEndBackward},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U';', U";"),
            "Older change position",
            "Change list",
            true,
            false,
            23},
        Command::ChangeListBack},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U',', U","),
            "Newer change position",
            "Change list",
            true,
            false,
            24},
        Command::ChangeListForward},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'i', U"i"),
            "Insert at last insert position",
            "Insert",
            true,
            false,
            25},
        Command::InsertAtLastPosition},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(
                U'g', U"<C-g>", KeyModifier::Control),
            "Detailed buffer status",
            "Status",
            true,
            false,
            26},
        Command::ShowDetailedBufferStatus},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'u', U"u"),
            "Lowercase operator",
            "Operator",
            false,
            true,
            27},
        Command::BeginLowercaseOperator},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'U', U"U"),
            "Uppercase operator",
            "Operator",
            false,
            true,
            28},
        Command::BeginUppercaseOperator},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'*', U"*"),
            "Search word forward (partial)",
            "Search",
            true,
            false,
            30},
        Command::SearchWordForwardPartial},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'#', U"#"),
            "Search word backward (partial)",
            "Search",
            true,
            false,
            31},
        Command::SearchWordBackwardPartial},
};

constexpr std::array kBracketLeftGrammar{
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'[', U"["),
            "Previous section start",
            "Section",
            true,
            false,
            10},
        Command::SectionBackwardStart},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U']', U"]"),
            "Previous section end",
            "Section",
            true,
            false,
            20},
        Command::SectionBackwardEnd},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'b', U"b"),
            "Previous buffer",
            "Buffer",
            true,
            false,
            30},
        Command::PreviousBuffer},
};

constexpr std::array kBracketRightGrammar{
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U']', U"]"),
            "Next section start",
            "Section",
            true,
            false,
            10},
        Command::SectionForwardStart},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'[', U"["),
            "Next section end",
            "Section",
            true,
            false,
            20},
        Command::SectionForwardEnd},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'b', U"b"),
            "Next buffer",
            "Buffer",
            true,
            false,
            30},
        Command::NextBuffer},
};

constexpr auto kWindowGrammar = [] {
    std::array<
        NativeSemanticCommandDescriptor,
        WindowCommands.size()>
        result{};
    for (std::size_t index = 0;
         index < WindowCommands.size();
         ++index) {
        const WindowCommandDescriptor &command =
            WindowCommands[index];
        result[index] = NativeSemanticCommandDescriptor{
            NativeHintDescriptor{
                characterKey(command.key, command.notation),
                command.description,
                "Window",
                true,
                false,
                command.order},
            command.commandId};
    }
    return result;
}();

// Neovim reads the character following CTRL-W literally and accepts both
// control-key and cursor-key spellings.  These aliases resolve back to the
// canonical command descriptor above, so they can never acquire independent
// execution or presentation metadata.
constexpr std::array kWindowAliases{
    NativeWindowAliasDescriptor{
        characterKey(U'w', U"<C-w>", KeyModifier::Control),
        WindowOperation::FocusNext},
    NativeWindowAliasDescriptor{
        characterKey(U'h', U"<C-h>", KeyModifier::Control),
        WindowOperation::FocusLeft},
    NativeWindowAliasDescriptor{
        characterKey(U'j', U"<C-j>", KeyModifier::Control),
        WindowOperation::FocusDown},
    NativeWindowAliasDescriptor{
        characterKey(U'k', U"<C-k>", KeyModifier::Control),
        WindowOperation::FocusUp},
    NativeWindowAliasDescriptor{
        characterKey(U'l', U"<C-l>", KeyModifier::Control),
        WindowOperation::FocusRight},
    NativeWindowAliasDescriptor{
        specialKey(SpecialKey::Backspace, U"<BS>"),
        WindowOperation::FocusLeft},
    NativeWindowAliasDescriptor{
        specialKey(SpecialKey::Left, U"<Left>"),
        WindowOperation::FocusLeft},
    NativeWindowAliasDescriptor{
        specialKey(SpecialKey::Down, U"<Down>"),
        WindowOperation::FocusDown},
    NativeWindowAliasDescriptor{
        specialKey(SpecialKey::Up, U"<Up>"),
        WindowOperation::FocusUp},
    NativeWindowAliasDescriptor{
        specialKey(SpecialKey::Right, U"<Right>"),
        WindowOperation::FocusRight},
    NativeWindowAliasDescriptor{
        characterKey(U't', U"<C-t>", KeyModifier::Control),
        WindowOperation::FocusFirst},
    NativeWindowAliasDescriptor{
        characterKey(U'b', U"<C-b>", KeyModifier::Control),
        WindowOperation::FocusLast},
    NativeWindowAliasDescriptor{
        characterKey(U'p', U"<C-p>", KeyModifier::Control),
        WindowOperation::FocusPreviouslyAccessed},
    NativeWindowAliasDescriptor{
        characterKey(U's', U"<C-s>", KeyModifier::Control),
        WindowOperation::SplitHorizontal},
    NativeWindowAliasDescriptor{
        characterKey(U'v', U"<C-v>", KeyModifier::Control),
        WindowOperation::SplitVertical},
    NativeWindowAliasDescriptor{
        characterKey(U'q', U"<C-q>", KeyModifier::Control),
        WindowOperation::Quit},
    NativeWindowAliasDescriptor{
        characterKey(U'o', U"<C-o>", KeyModifier::Control),
        WindowOperation::CloseOthers},
    NativeWindowAliasDescriptor{
        characterKey(U'x', U"<C-x>", KeyModifier::Control),
        WindowOperation::ExchangeNext},
    NativeWindowAliasDescriptor{
        characterKey(U'r', U"<C-r>", KeyModifier::Control),
        WindowOperation::RotateForward},
    NativeWindowAliasDescriptor{
        characterKey(U'_', U"<C-_>", KeyModifier::Control),
        WindowOperation::MaximizeHeight},
    NativeWindowAliasDescriptor{
        characterKey(
            U'_',
            U"<C-_>",
            KeyModifier::Control | KeyModifier::Shift),
        WindowOperation::MaximizeHeight},
};

[[nodiscard]] inline const NativeSemanticCommandDescriptor *
findWindowDescriptor(const KeyAtom &key) noexcept
{
    if (const auto *const direct =
            findNativeDescriptor(kWindowGrammar, key)) {
        return direct;
    }
    const auto alias = std::ranges::find_if(
        kWindowAliases,
        [&key](const NativeWindowAliasDescriptor &candidate) {
            return nativeKeyMatches(candidate.key, key);
        });
    if (alias == kWindowAliases.cend()) {
        return nullptr;
    }
    const WindowCommandDescriptor *const command =
        windowCommand(alias->operation);
    if (command == nullptr) {
        return nullptr;
    }
    const auto descriptor = std::ranges::find_if(
        kWindowGrammar,
        [command](const NativeSemanticCommandDescriptor &candidate) {
            return candidate.commandId == command->commandId;
        });
    return descriptor == kWindowGrammar.cend()
        ? nullptr
        : &*descriptor;
}

constexpr std::array kViewportGrammar{
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U't', U"t"),
            "Cursor line at top",
            "Viewport",
            true,
            false,
            10},
        Command::ViewCursorAtTop},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'z', U"z"),
            "Cursor line at center",
            "Viewport",
            true,
            false,
            20},
        Command::ViewCursorAtCenter},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'b', U"b"),
            "Cursor line at bottom",
            "Viewport",
            true,
            false,
            30},
        Command::ViewCursorAtBottom},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'h', U"h"),
            "Scroll view left",
            "Viewport",
            true,
            false,
            40},
        Command::ViewScrollLeft},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'l', U"l"),
            "Scroll view right",
            "Viewport",
            true,
            false,
            50},
        Command::ViewScrollRight},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'H', U"H"),
            "Scroll view half-screen left",
            "Viewport",
            true,
            false,
            60},
        Command::ViewScrollHalfLeft},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'L', U"L"),
            "Scroll view half-screen right",
            "Viewport",
            true,
            false,
            70},
        Command::ViewScrollHalfRight},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U's', U"s"),
            "Cursor column at start",
            "Viewport",
            true,
            false,
            80},
        Command::ViewCursorAtStart},
    NativeCommandDescriptor{
        NativeHintDescriptor{
            characterKey(U'e', U"e"),
            "Cursor column at end",
            "Viewport",
            true,
            false,
            90},
        Command::ViewCursorAtEnd},
};

constexpr std::array kRegisterGrammar{
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'"', U"\""),
            "Unnamed register",
            "Register",
            true,
            false,
            10}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'-', U"-"),
            "Small delete register",
            "Register",
            true,
            false,
            20}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            decimalDigitKey(U"0…9"),
            "Numbered register",
            "Register",
            true,
            false,
            30}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            lowercaseLetterKey(U"a…z"),
            "Named register",
            "Register",
            true,
            false,
            40}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            uppercaseLetterKey(U"A…Z"),
            "Append to named register",
            "Register",
            true,
            false,
            50}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'_', U"_"),
            "Black-hole register",
            "Register",
            true,
            false,
            60}},
};

constexpr std::array kMarkGrammar{
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            lowercaseLetterKey(U"a…z"),
            "Buffer-local mark",
            "Marks",
            true,
            false,
            10}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            uppercaseLetterKey(U"A…Z"),
            "Global mark",
            "Marks",
            true,
            false,
            20}},
};

constexpr std::array kJumpMarkGrammar{
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            lowercaseLetterKey(U"a…z"),
            "Buffer-local mark",
            "Marks",
            true,
            false,
            10}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            uppercaseLetterKey(U"A…Z"),
            "Global mark",
            "Marks",
            true,
            false,
            20}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'\'', U"'"),
            "Position before latest jump",
            "Special marks",
            true,
            false,
            30}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'`', U"`"),
            "Position before latest jump",
            "Special marks",
            true,
            false,
            31}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'.', U"."),
            "Last change",
            "Special marks",
            true,
            false,
            32}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'^', U"^"),
            "Last Insert exit",
            "Special marks",
            true,
            false,
            33}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'[', U"["),
            "Start of last operation",
            "Special marks",
            true,
            false,
            34}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U']', U"]"),
            "End of last operation",
            "Special marks",
            true,
            false,
            35}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'<', U"<"),
            "Start of last Visual area",
            "Special marks",
            true,
            false,
            36}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'>', U">"),
            "End of last Visual area",
            "Special marks",
            true,
            false,
            37}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'\"', U"\""),
            "Cursor when leaving buffer",
            "Special marks",
            true,
            false,
            38}},
};

constexpr std::array kMacroRegisterGrammar{
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            lowercaseLetterKey(U"a…z"),
            "Record or execute macro",
            "Macros",
            true,
            false,
            10}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            uppercaseLetterKey(U"A…Z"),
            "Append macro recording",
            "Macros",
            true,
            false,
            20}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'@', U"@"),
            "Repeat last executed macro",
            "Macros",
            true,
            false,
            30}},
};

constexpr std::array kOperators{
    NativeOperatorDescriptor{
        NativeHintDescriptor{
            characterKey(U'd', U"d"),
            "Delete line",
            "Operator",
            true,
            false,
            10},
        OperatorKind::Delete},
    NativeOperatorDescriptor{
        NativeHintDescriptor{
            characterKey(U'c', U"c"),
            "Change line",
            "Operator",
            true,
            false,
            10},
        OperatorKind::Change},
    NativeOperatorDescriptor{
        NativeHintDescriptor{
            characterKey(U'y', U"y"),
            "Yank line",
            "Operator",
            true,
            false,
            10},
        OperatorKind::Yank},
    NativeOperatorDescriptor{
        NativeHintDescriptor{
            characterKey(U'>', U">"),
            "Shift lines right",
            "Operator",
            true,
            false,
            20},
        OperatorKind::ShiftRight},
    NativeOperatorDescriptor{
        NativeHintDescriptor{
            characterKey(U'<', U"<"),
            "Shift lines left",
            "Operator",
            true,
            false,
            21},
        OperatorKind::ShiftLeft},
};

constexpr std::array kOperatorContinuations{
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'g', U"g"),
            "Goto motion",
            "Goto",
            false,
            true,
            20},
        OperatorContinuationKind::GotoPrefix,
        Command::MoveLeft},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'h', U"h"),
            "Character left",
            "Motion",
            true,
            false,
            30},
        OperatorContinuationKind::Motion,
        Command::MoveLeft},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(
                U'h', U"<C-h>", KeyModifier::Control),
            "Character left",
            "Motion",
            true,
            false,
            30},
        OperatorContinuationKind::Motion,
        Command::MoveLeft},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            specialKey(SpecialKey::Backspace, U"<BS>"),
            "Character left",
            "Motion",
            true,
            false,
            30},
        OperatorContinuationKind::Motion,
        Command::MoveLeft},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'j', U"j"),
            "Line down",
            "Motion",
            true,
            false,
            31},
        OperatorContinuationKind::Motion,
        Command::MoveDown},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(
                U'j', U"<C-j>", KeyModifier::Control),
            "Line down",
            "Motion",
            true,
            false,
            31},
        OperatorContinuationKind::Motion,
        Command::MoveDown},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(
                U'n', U"<C-n>", KeyModifier::Control),
            "Line down",
            "Motion",
            true,
            false,
            31},
        OperatorContinuationKind::Motion,
        Command::MoveDown},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'k', U"k"),
            "Line up",
            "Motion",
            true,
            false,
            32},
        OperatorContinuationKind::Motion,
        Command::MoveUp},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(
                U'p', U"<C-p>", KeyModifier::Control),
            "Line up",
            "Motion",
            true,
            false,
            32},
        OperatorContinuationKind::Motion,
        Command::MoveUp},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'l', U"l"),
            "Character right",
            "Motion",
            true,
            false,
            33},
        OperatorContinuationKind::Motion,
        Command::MoveRight},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'w', U"w"),
            "Next word",
            "Motion",
            true,
            false,
            34},
        OperatorContinuationKind::Motion,
        Command::WordForward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'b', U"b"),
            "Previous word",
            "Motion",
            true,
            false,
            35},
        OperatorContinuationKind::Motion,
        Command::WordBackward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'e', U"e"),
            "End of word",
            "Motion",
            true,
            false,
            36},
        OperatorContinuationKind::Motion,
        Command::WordEnd},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'W', U"W"),
            "Next WORD",
            "Motion",
            true,
            false,
            37},
        OperatorContinuationKind::Motion,
        Command::BigWordForward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'B', U"B"),
            "Previous WORD",
            "Motion",
            true,
            false,
            38},
        OperatorContinuationKind::Motion,
        Command::BigWordBackward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'E', U"E"),
            "End of WORD",
            "Motion",
            true,
            false,
            39},
        OperatorContinuationKind::Motion,
        Command::BigWordEnd},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'(', U"("),
            "Previous sentence",
            "Motion",
            true,
            false,
            39},
        OperatorContinuationKind::Motion,
        Command::SentenceBackward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U')', U")"),
            "Next sentence",
            "Motion",
            true,
            false,
            40},
        OperatorContinuationKind::Motion,
        Command::SentenceForward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'{', U"{"),
            "Previous paragraph",
            "Motion",
            true,
            false,
            41},
        OperatorContinuationKind::Motion,
        Command::ParagraphBackward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'}', U"}"),
            "Next paragraph",
            "Motion",
            true,
            false,
            42},
        OperatorContinuationKind::Motion,
        Command::ParagraphForward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'[', U"["),
            "Previous section prefix",
            "Section",
            false,
            true,
            43},
        OperatorContinuationKind::BracketLeftPrefix,
        Command::SectionBackwardStart},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U']', U"]"),
            "Next section prefix",
            "Section",
            false,
            true,
            44},
        OperatorContinuationKind::BracketRightPrefix,
        Command::SectionForwardStart},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'0', U"0"),
            "Start of line",
            "Motion",
            true,
            false,
            40},
        OperatorContinuationKind::Motion,
        Command::LineStart},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'^', U"^"),
            "First non-blank character",
            "Motion",
            true,
            false,
            41},
        OperatorContinuationKind::Motion,
        Command::FirstNonBlank},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'$', U"$"),
            "End of line",
            "Motion",
            true,
            false,
            42},
        OperatorContinuationKind::Motion,
        Command::LineEnd},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'+', U"+"),
            "Next line first non-blank",
            "Motion",
            true,
            false,
            43},
        OperatorContinuationKind::Motion,
        Command::LineDownFirstNonBlank},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            specialKey(SpecialKey::Enter, U"<CR>"),
            "Next line first non-blank",
            "Motion",
            true,
            false,
            44},
        OperatorContinuationKind::Motion,
        Command::LineDownFirstNonBlank},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(
                U'm', U"<C-m>", KeyModifier::Control),
            "Next line first non-blank",
            "Motion",
            true,
            false,
            44},
        OperatorContinuationKind::Motion,
        Command::LineDownFirstNonBlank},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'-', U"-"),
            "Previous line first non-blank",
            "Motion",
            true,
            false,
            45},
        OperatorContinuationKind::Motion,
        Command::LineUpFirstNonBlank},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'_', U"_"),
            "Current line first non-blank",
            "Motion",
            true,
            false,
            45},
        OperatorContinuationKind::Motion,
        Command::CurrentLineFirstNonBlank},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'|', U"|"),
            "Screen column",
            "Motion",
            true,
            false,
            45},
        OperatorContinuationKind::Motion,
        Command::ScreenColumn},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'G', U"G"),
            "Last line",
            "Motion",
            true,
            false,
            46},
        OperatorContinuationKind::Motion,
        Command::LastLine},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'H', U"H"),
            "Window top",
            "Motion",
            true,
            false,
            47},
        OperatorContinuationKind::Motion,
        Command::WindowTop},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'M', U"M"),
            "Window middle",
            "Motion",
            true,
            false,
            48},
        OperatorContinuationKind::Motion,
        Command::WindowMiddle},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'L', U"L"),
            "Window bottom",
            "Motion",
            true,
            false,
            49},
        OperatorContinuationKind::Motion,
        Command::WindowBottom},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'f', U"f"),
            "Find character forward",
            "Motion",
            false,
            true,
            50},
        OperatorContinuationKind::CharacterSearch,
        Command::BeginFindForward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'F', U"F"),
            "Find character backward",
            "Motion",
            false,
            true,
            51},
        OperatorContinuationKind::CharacterSearch,
        Command::BeginFindBackward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U't', U"t"),
            "Till character forward",
            "Motion",
            false,
            true,
            52},
        OperatorContinuationKind::CharacterSearch,
        Command::BeginTillForward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'T', U"T"),
            "Till character backward",
            "Motion",
            false,
            true,
            53},
        OperatorContinuationKind::CharacterSearch,
        Command::BeginTillBackward},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U';', U";"),
            "Repeat character search",
            "Motion",
            true,
            false,
            54},
        OperatorContinuationKind::RepeatCharacterSearch,
        Command::RepeatCharacterSearch},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U',', U","),
            "Repeat search opposite",
            "Motion",
            true,
            false,
            55},
        OperatorContinuationKind::RepeatCharacterSearch,
        Command::RepeatCharacterSearchOpposite},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'%', U"%"),
            "Matching pair",
            "Motion",
            true,
            false,
            56},
        OperatorContinuationKind::MatchingPair,
        Command::MatchPair},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'i', U"i"),
            "Inside text object",
            "Text object",
            false,
            true,
            60},
        OperatorContinuationKind::TextObjectPrefix,
        Command::MoveLeft},
    NativeOperatorContinuationDescriptor{
        NativeHintDescriptor{
            characterKey(U'a', U"a"),
            "Around text object",
            "Text object",
            false,
            true,
            61},
        OperatorContinuationKind::TextObjectPrefix,
        Command::MoveLeft},
};

constexpr std::array kTextObjectGrammar{
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'w', U"w"),
            "Word",
            "Text object",
            true,
            false,
            10}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'W', U"W"),
            "WORD",
            "Text object",
            true,
            false,
            20}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'\'', U"'"),
            "Single-quoted string",
            "Text object",
            true,
            false,
            30}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'\"', U"\""),
            "Double-quoted string",
            "Text object",
            true,
            false,
            31}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'`', U"`"),
            "Backtick string",
            "Text object",
            true,
            false,
            32}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'(', U"("),
            "Parentheses",
            "Text object",
            true,
            false,
            40}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U')', U")"),
            "Parentheses",
            "Text object",
            true,
            false,
            41}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'b', U"b"),
            "Parentheses",
            "Text object",
            true,
            false,
            42}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'[', U"["),
            "Square brackets",
            "Text object",
            true,
            false,
            50}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U']', U"]"),
            "Square brackets",
            "Text object",
            true,
            false,
            51}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'{', U"{"),
            "Braces",
            "Text object",
            true,
            false,
            60}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'}', U"}"),
            "Braces",
            "Text object",
            true,
            false,
            61}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'B', U"B"),
            "Braces",
            "Text object",
            true,
            false,
            62}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'<', U"<"),
            "Angle brackets",
            "Text object",
            true,
            false,
            63}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'>', U">"),
            "Angle brackets",
            "Text object",
            true,
            false,
            64}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U't', U"t"),
            "Tag block",
            "Text object",
            true,
            false,
            65}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U'p', U"p"),
            "Paragraph",
            "Text object",
            true,
            false,
            70}},
    NativeRegisterDescriptor{
        NativeHintDescriptor{
            characterKey(U's', U"s"),
            "Sentence",
            "Text object",
            true,
            false,
            80}},
};

constexpr NativeHintDescriptor kCharacterArgumentHint{
    anyCharacterKey(U"<char>"),
    "Character",
    "Character",
    true,
    false,
    10};

/**
 * The complete argument grammar accepted after Normal-mode `r`.
 *
 * Both hint enumeration and execution consume this table. This matters for
 * special keys: Neovim accepts Enter and Tab as replacement characters even
 * though neither is represented by a printable-character KeyAtom.
 */
constexpr std::array kReplacementGrammar{
    NativeReplacementDescriptor{
        NativeHintDescriptor{
            anyCharacterKey(U"<char>"),
            "Replace with character",
            "Replace",
            true,
            false,
            10},
        U'\0'},
    NativeReplacementDescriptor{
        NativeHintDescriptor{
            specialKey(SpecialKey::Enter, U"<CR>"),
            "Replace with line break",
            "Replace",
            true,
            false,
            20},
        U'\n'},
    NativeReplacementDescriptor{
        NativeHintDescriptor{
            specialKey(SpecialKey::Tab, U"<Tab>"),
            "Replace with tab",
            "Replace",
            true,
            false,
            30},
        U'\t'},
};

enum class NativeInsertCommandKind : std::uint8_t
{
    DeletePreviousWord,
    DeleteToLineStart,
    BeginRegisterInsert,
    ExecuteOneNormal
};

struct NativeInsertCommandDescriptor final
{
    NativeHintDescriptor hint;
    NativeInsertCommandKind kind =
        NativeInsertCommandKind::DeletePreviousWord;
};

/**
 * Insert control commands participate in the same canonical native grammar
 * as Normal commands. The Qt boundary never compares platform key enums for
 * these operations, and user/plugin Insert mappings retain resolver priority.
 */
constexpr std::array kInsertCommandGrammar{
    NativeInsertCommandDescriptor{
        NativeHintDescriptor{
            characterKey(
                U'w', U"<C-w>", KeyModifier::Control),
            "Delete previous word",
            "Insert",
            true,
            false,
            10},
        NativeInsertCommandKind::DeletePreviousWord},
    NativeInsertCommandDescriptor{
        NativeHintDescriptor{
            characterKey(
                U'u', U"<C-u>", KeyModifier::Control),
            "Delete to line start",
            "Insert",
            true,
            false,
            20},
        NativeInsertCommandKind::DeleteToLineStart},
    NativeInsertCommandDescriptor{
        NativeHintDescriptor{
            characterKey(
                U'r', U"<C-r>", KeyModifier::Control),
            "Insert register",
            "Insert",
            false,
            true,
            30},
        NativeInsertCommandKind::BeginRegisterInsert},
    NativeInsertCommandDescriptor{
        NativeHintDescriptor{
            characterKey(
                U'o', U"<C-o>", KeyModifier::Control),
            "Execute one Normal command",
            "Insert",
            false,
            true,
            40},
        NativeInsertCommandKind::ExecuteOneNormal},
};

constexpr NativeHintDescriptor kOneNormalCommandHint{
    anyCharacterKey(U"<normal>"),
    "One complete Normal command",
    "Insert CTRL-O",
    true,
    false,
    10};


} // namespace vkui::vk::core_detail
