#pragma once

// Private state declaration shared only by VkCore translation units. It is
// neither installed nor available to QWidget/plugin code.
#include "VkCore.h"

#include <vkui/buffer/BufferStorage.h>

#include "VkCanonicalKeyEvent.h"
#include "VkInputEngine.h"
#include "VkWindowCommands.h"

#include <QChar>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QString>
#include <QTextBoundaryFinder>

#include <unicode/uchar.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vkui::vk {
namespace core_detail {
using detail::KeyAtom;
using detail::KeyModifier;
using detail::SpecialKey;

enum class Command : std::uint8_t
{
    MoveLeft,
    MoveDown,
    MoveUp,
    MoveRight,
    WordForward,
    WordBackward,
    WordEnd,
    WordEndBackward,
    BigWordForward,
    BigWordBackward,
    BigWordEnd,
    BigWordEndBackward,
    SentenceBackward,
    SentenceForward,
    ParagraphBackward,
    ParagraphForward,
    SectionBackwardStart,
    SectionForwardStart,
    SectionBackwardEnd,
    SectionForwardEnd,
    LineStart,
    FirstNonBlank,
    LineEnd,
    LastNonBlank,
    LineDownFirstNonBlank,
    LineUpFirstNonBlank,
    CurrentLineFirstNonBlank,
    ScreenColumn,
    FirstLine,
    LastLine,
    FilePercent,
    TextMiddle,
    DisplayRowUp,
    DisplayRowDown,
    DisplayRowStart,
    DisplayRowFirstNonBlank,
    DisplayRowEnd,
    DisplayRowMiddle,
    WindowTop,
    WindowMiddle,
    WindowBottom,
    MatchPair,
    BeginFindForward,
    BeginFindBackward,
    BeginTillForward,
    BeginTillBackward,
    RepeatCharacterSearch,
    RepeatCharacterSearchOpposite,
    EnterInsert,
    InsertAtLastPosition,
    AppendInsert,
    InsertAtLineStart,
    AppendAtLineEnd,
    EnterReplaceMode,
    EnterVisual,
    EnterVisualLine,
    EnterVisualBlock,
    PreviousBuffer,
    NextBuffer,
    AddNumber,
    SubtractNumber,
    ShowBufferStatus,
    ShowDetailedBufferStatus,
    BeginLowercaseOperator,
    BeginUppercaseOperator,
    Undo,
    Redo,
    ScrollHalfDown,
    ScrollHalfUp,
    ScrollPageDown,
    ScrollPageUp,
    ScrollLineDown,
    ScrollLineUp,
    ViewCursorAtTop,
    ViewCursorAtCenter,
    ViewCursorAtBottom,
    ViewScrollLeft,
    ViewScrollRight,
    ViewScrollHalfLeft,
    ViewScrollHalfRight,
    ViewCursorAtStart,
    ViewCursorAtEnd,
    BeginViewportPosition,
    BeginRegisterSelect,
    DeleteUnderCursor,
    DeleteBeforeCursor,
    PutAfter,
    PutBefore,
    OpenBelow,
    OpenAbove,
    BeginReplace,
    SubstituteCharacters,
    SubstituteLines,
    DeleteToLineEnd,
    ChangeToLineEnd,
    JoinLines,
    ToggleCase,
    RepeatLastChange,
    BeginSetMark,
    BeginJumpMarkLine,
    BeginJumpMarkExact,
    JumpListBack,
    JumpListForward,
    ChangeListBack,
    ChangeListForward,
    AlternateBuffer,
    BeginExCommand,
    BeginSearchForward,
    BeginSearchBackward,
    RepeatSearch,
    RepeatSearchOpposite,
    SearchWordForwardExact,
    SearchWordBackwardExact,
    SearchWordForwardPartial,
    SearchWordBackwardPartial,
    BeginMacroRecord,
    BeginMacroExecute,
    RepeatMacroExecute
};

[[nodiscard]] constexpr bool linewiseMotion(
    const Command command) noexcept
{
    return command == Command::MoveDown
        || command == Command::MoveUp
        || command
            == Command::LineDownFirstNonBlank
        || command
            == Command::LineUpFirstNonBlank
        || command
            == Command::CurrentLineFirstNonBlank
        || command == Command::FirstLine
        || command == Command::LastLine
        || command == Command::FilePercent
        || command == Command::WindowTop
        || command == Command::WindowMiddle
        || command == Command::WindowBottom;
}

[[nodiscard]] constexpr bool inclusiveMotion(
    const Command command) noexcept
{
    return command == Command::WordEnd
        || command == Command::WordEndBackward
        || command == Command::BigWordEnd
        || command == Command::BigWordEndBackward
        || command == Command::LineEnd
        || command == Command::LastNonBlank
        || command == Command::DisplayRowEnd
        || command == Command::MatchPair;
}

[[nodiscard]] constexpr bool displayMotion(
    const Command command) noexcept
{
    return command == Command::DisplayRowUp
        || command == Command::DisplayRowDown
        || command == Command::DisplayRowStart
        || command == Command::DisplayRowFirstNonBlank
        || command == Command::DisplayRowEnd
        || command == Command::DisplayRowMiddle;
}

[[nodiscard]] constexpr bool verticalDisplayMotion(
    const Command command) noexcept
{
    return command == Command::DisplayRowUp
        || command == Command::DisplayRowDown;
}

[[nodiscard]] constexpr DisplayMotionKind displayMotionKind(
    const Command command) noexcept
{
    switch (command) {
    case Command::DisplayRowUp:
        return DisplayMotionKind::RowUp;
    case Command::DisplayRowDown:
        return DisplayMotionKind::RowDown;
    case Command::DisplayRowStart:
        return DisplayMotionKind::RowStart;
    case Command::DisplayRowFirstNonBlank:
        return DisplayMotionKind::RowFirstNonBlank;
    case Command::DisplayRowEnd:
        return DisplayMotionKind::RowEnd;
    case Command::DisplayRowMiddle:
        return DisplayMotionKind::RowMiddle;
    default:
        return DisplayMotionKind::RowDown;
    }
}

enum class CommandPrefix : std::uint8_t
{
    None,
    G,
    BracketLeft,
    BracketRight,
    Window
};

enum class OperatorKind : std::uint8_t
{
    Delete,
    Change,
    Yank,
    ShiftRight,
    ShiftLeft,
    Lowercase,
    Uppercase
};

struct PendingOperator final
{
    OperatorKind kind = OperatorKind::Delete;
    BufferId buffer = 0;
    Cursor start;
    std::size_t count = 1;
    bool countWasExplicit = false;
};

enum class PendingArgumentKind : std::uint8_t
{
    ReplaceCharacter,
    ViewportPosition,
    SelectRegister,
    CharacterSearch,
    SetMark,
    JumpMarkLine,
    JumpMarkExact,
    TextObject,
    MacroRecord,
    MacroExecute,
    InsertRegister
};

struct PendingArgument final
{
    PendingArgumentKind kind =
        PendingArgumentKind::ReplaceCharacter;
    WindowId window = 0;
    BufferId buffer = 0;
    std::size_t count = 1;
    bool countWasExplicit = false;
    bool searchForward = true;
    bool searchTill = false;
    bool around = false;
};

enum class NativeKeyKind : std::uint8_t
{
    Character,
    Special,
    DecimalDigit,
    LowercaseLetter,
    UppercaseLetter,
    AnyCharacter
};

/**
 * Canonical key identity plus its stable UI notation.
 *
 * Native grammar execution and input-hint enumeration both consume these
 * descriptors. Keeping the notation beside the matcher prevents the command
 * grammar and which-key presentation from silently drifting apart.
 */
struct NativeKeyDescriptor final
{
    NativeKeyKind kind = NativeKeyKind::Character;
    char32_t character = U'\0';
    SpecialKey special = SpecialKey::Escape;
    KeyModifier modifiers = KeyModifier::None;
    std::u32string_view notation;
};

struct NativeHintDescriptor final
{
    NativeKeyDescriptor key;
    std::string_view description;
    std::string_view group;
    bool completes = true;
    bool hasChildren = false;
    int order = 0;
};

struct CharacterSearch final
{
    char32_t target = U'\0';
    bool forward = true;
    bool till = false;
};

struct RegisterValue final
{
    std::u16string text;
    bool linewise = false;
    bool blockwise = false;
    // Vim stores the screen-cell width in a block register independently of
    // its UTF-16 payload (`getregtype()` reports ^V{width}). This is required
    // for tabs, wide graphemes, appends, and later block puts.
    std::size_t blockDisplayWidth = 0;
    // Macros and text operations address the same named registers in Vim.
    // Keep the lossless key representation beside the text payload instead
    // of maintaining a second, silently divergent macro register bank.
    detail::KeySequence macroKeys;

    [[nodiscard]] bool empty() const noexcept
    {
        return text.empty() && macroKeys.empty();
    }
};

enum class RepeatForm : std::uint8_t
{
    Command,
    OperatorMotion,
    OperatorLinewise,
    OperatorTextObject,
    VisualCharacter,
    VisualBlock,
    VisualBlockInsert,
    VisualBlockPut,
    VisualBlockReplace,
    VisualBlockToggle,
    VisualTransform
};

enum class ReplaceActionKind : std::uint8_t
{
    Overwrite,
    Delete
};

struct ReplaceAction final
{
    ReplaceActionKind kind = ReplaceActionKind::Overwrite;
    std::u16string text;
};

/**
 * Native command result relevant to macro playback.
 *
 * Neovim aborts a macro for failed character searches and failed text
 * objects, while several other command failures (`%`, `n`, unset marks,
 * read-only edits) leave later macro keys runnable. Keep that distinction as
 * typed execution state rather than inferring it from diagnostic strings.
 */
enum class MacroCommandDisposition : std::uint8_t
{
    Continue,
    Abort
};

struct RepeatChange final
{
    RepeatForm form = RepeatForm::Command;
    Command command = Command::DeleteUnderCursor;
    std::size_t count = 1;
    char32_t argument = U'\0';
    OperatorKind operation = OperatorKind::Delete;
    Command motion = Command::MoveRight;
    bool motionCountWasExplicit = false;
    RegisterValue put;
    std::u16string insertedText;
    std::vector<ReplaceAction> replaceActions;
    bool changedBeforeInsert = false;
    char32_t textObject = U'\0';
    bool textObjectAround = false;
    std::size_t blockHeight = 1;
    std::size_t blockWidth = 1;
    std::size_t visualEndColumn = 0;
    VisualKind visualKind = VisualKind::Character;
    bool preserveVisualShape = false;
    // A register prefix is part of Vim's redo recipe.  Keeping it here makes
    // `.` repeat both the edit and its register side effects.
    char32_t registerName = U'"';
};

/**
 * One Insert-mode keyword-completion transaction.
 *
 * The original prefix remains in the buffer.  Only the suffix inserted at
 * `insertionOffset` is replaced while cycling, which is how Vim keeps the
 * completion inside the current Insert undo block and dot-repeat recipe.
 */
struct KeywordCompletionState final
{
    WindowId window = 0;
    BufferId buffer = 0;
    std::size_t insertionOffset = 0;
    std::size_t replacementLength = 0;
    std::u16string prefix;
    // Option zero is the uncompleted prefix (an empty inserted suffix).
    // Remaining entries are complete words in forward buffer-search order.
    std::vector<std::u16string> options;
    std::size_t selected = 0;
    std::uint64_t revision = 0;
};

struct BlockInsertState final
{
    WindowId window = 0;
    BufferId buffer = 0;
    std::size_t firstLine = 0;
    std::size_t lastLine = 0;
    std::size_t displayColumn = 0;
    bool padShortLines = true;
};

/** One reversible overwrite made by the active Replace-mode segment. */
struct ReplaceStep final
{
    std::size_t offset = 0;
    std::u16string removed;
    std::u16string inserted;
    Cursor before;
    Cursor after;
    std::size_t actionIndex = 0;
};

/**
 * Replace mode is an edit state, not a Qt typing mode.
 *
 * Vim restores overwritten characters when Backspace walks back through the
 * current replacement segment.  Arrow motions start a new undo/repeat
 * segment, so only that segment is retained here.
 */
struct ReplaceSession final
{
    WindowId window = 0;
    BufferId buffer = 0;
    std::size_t repeatCount = 1;
    bool repeatCountActive = true;
    std::vector<ReplaceStep> steps;
    std::vector<ReplaceAction> actions;
};

struct BlockInsertionEdit final
{
    std::size_t bufferStart = 0;
    std::size_t bufferEnd = 0;
    std::u16string replacement;
    std::size_t insertionOffset = 0;
    std::size_t resultingColumn = 0;
};

struct VisualSelection final
{
    WindowId window = 0;
    BufferId buffer = 0;
    Cursor anchor;
    std::size_t anchorDisplayColumn = 0;
    VisualKind kind = VisualKind::Character;
};

struct DisplayCell final
{
    std::size_t bufferStart = 0;
    std::size_t bufferEnd = 0;
    std::size_t displayStart = 0;
    std::size_t displayEnd = 0;
    bool tab = false;
};

struct DisplayLine final
{
    // Ordinary one-UTF-16-unit/one-screen-cell graphemes are implicit. Only
    // nonlinear cells (tabs, wide glyphs, surrogate pairs, and multi-unit
    // grapheme clusters) need storage. This keeps the hot ASCII path compact:
    // a multi-megabyte line has one DisplayLine and zero DisplayCells.
    std::vector<DisplayCell> specialCells;
    std::size_t bufferLength = 0;
    std::size_t width = 0;
};


[[nodiscard]] std::vector<std::size_t> lineStarts(
    const std::u16string &text);
[[nodiscard]] bool plainCharacter(
    const KeyAtom &key,
    char32_t expected) noexcept;
[[nodiscard]] bool modifiedCharacter(
    const KeyAtom &key,
    char32_t expected,
    KeyModifier modifier) noexcept;
[[nodiscard]] bool isCoreHandledInsertKey(
    const KeyAtom &key) noexcept;
[[nodiscard]] bool isInsertEditingCommandKey(
    const KeyAtom &key) noexcept;
[[nodiscard]] bool isInsertNavigationKey(
    const KeyAtom &key) noexcept;
[[nodiscard]] bool isInsertKeywordCompletionKey(
    const KeyAtom &key) noexcept;
[[nodiscard]] MappingMode mappingModeFor(Mode mode) noexcept;
void appendUtf16(std::u16string &target, char32_t scalar);
[[nodiscard]] detail::KeySequence macroKeysFromText(
    std::u16string_view text);
void appendRecordedKeyText(
    std::u16string &text,
    const KeyAtom &key);
[[nodiscard]] bool pathSeparator(char value) noexcept;
[[nodiscard]] std::string_view normalizedPathRoot(
    std::string_view root) noexcept;
[[nodiscard]] bool pathAtOrBelow(
    std::string_view path,
    std::string_view rawRoot) noexcept;

struct StringViewHash final
{
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(
        const std::string_view value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }
};

} // namespace core_detail

// State ownership remains singular even though behavior is compiled by
// subsystem. This using-directive is confined to this private header.
using namespace core_detail;

class VkCore::Implementation final
{
public:
    struct Location final
    {
        BufferId buffer = 0;
        std::size_t offset = 0;

        friend bool operator==(
            const Location &,
            const Location &) = default;
    };

    struct TextRange final
    {
        std::size_t start = 0;
        std::size_t end = 0;
        bool linewise = false;
    };

    struct EditDelta final
    {
        std::size_t offset = 0;
        std::u16string removed;
        std::u16string inserted;
    };

    struct WindowCursorState final
    {
        WindowId window = 0;
        Cursor cursor;
        std::optional<std::size_t> selectionAnchorOffset;
    };

    struct AuthoritativeSelectionOffsetState final
    {
        ViewId view = 0;
        std::size_t anchor = 0;
        std::size_t cursor = 0;
    };

    struct UndoNode final
    {
        static constexpr std::size_t noNode =
            std::numeric_limits<std::size_t>::max();

        std::size_t parent = noNode;
        std::vector<std::size_t> children;
        std::size_t preferredChild = noNode;
        std::vector<EditDelta> deltas;
        std::vector<WindowCursorState> beforeCursors;
        std::vector<WindowCursorState> afterCursors;
    };

    struct UndoHistory final
    {
        UndoHistory()
        {
            nodes.emplace_back();
        }

        std::vector<UndoNode> nodes;
        std::size_t current = 0;
        std::optional<std::size_t> clean = 0;
        std::optional<std::size_t> openInsertNode;
    };

    struct Buffer final
    {
        BufferId id = 0;
        std::string path;
        vkui::buffer::BufferStorage storage;
        std::vector<std::size_t> lineStarts{0};
        std::size_t tabStop = 8;
        // Line layouts are derived data. A text edit clears this sparse cache
        // in O(1); it never allocates one entry per line. Within each cached
        // line, ordinary width-one cells are represented implicitly.
        mutable std::unordered_map<std::size_t, DisplayLine>
            displayLines;
        bool readOnly = false;
        UndoHistory undo;
        std::array<std::optional<std::size_t>, 26>
            localMarks;
        std::optional<std::size_t> lastChangeMark;
        std::optional<std::size_t> lastInsertExitMark;
        std::optional<std::size_t> lastOperationStartMark;
        std::optional<std::size_t> lastOperationEndMark;
        std::optional<std::size_t> lastVisualStartMark;
        std::optional<std::size_t> lastVisualEndMark;
        std::optional<std::size_t> lastCursorMark;
        std::optional<std::size_t> lastOperationUndoNode;
        // Neovim stores change positions in the buffer while each window
        // owns its traversal index. Offsets keep anchors compact and are
        // repaired incrementally by mutateBuffer().
        std::vector<std::size_t> changeList;

        [[nodiscard]] const std::u16string &text() const noexcept
        {
            // VkCore creates owned editor storage only. Provider-backed
            // buffers enter through the generic VKBuffer data plane after
            // cursor/motion algorithms have migrated to bounded reads.
            return storage.ownedText()->text();
        }

        [[nodiscard]] std::uint64_t revision() const noexcept
        {
            return storage.ownedText()->revision();
        }

        [[nodiscard]] bool replaceText(
            std::u16string replacement)
        {
            return storage.replaceOwned(
                       std::move(replacement))
                       .status
                == vkui::buffer::EditStatus::Applied;
        }

        [[nodiscard]] bool editText(
            const std::size_t offset,
            const std::size_t removed,
            const std::u16string_view inserted)
        {
            return storage.editOwned(
                       offset,
                       removed,
                       inserted,
                       revision())
                       .status
                == vkui::buffer::EditStatus::Applied;
        }

        [[nodiscard]] std::optional<BufferSnapshot>
        snapshot() const
        {
            if (!storage.isOwned()) {
                return std::nullopt;
            }
            return BufferSnapshot{id, path, text(), revision()};
        }
    };

    struct View final
    {
        ViewKind kind = ViewKind::Other;
        BufferId buffer = 0;
        std::unordered_map<BufferId, Cursor> cursors;
        // Native non-modal hosts may keep a selection independently of
        // Neovim Visual mode. The optional anchor is a canonical UTF-16
        // buffer offset; absence means a collapsed selection at cursors[].
        std::optional<std::size_t> selectionAnchorOffset;
        std::unordered_map<BufferId, std::size_t>
            displayColumns;
        // Vim's curswant: a display-cell goal, never a UTF-16 buffer column.
        std::optional<std::size_t> preferredColumn;
        // Wrapped-row equivalent of curswant. Only a renderer can measure
        // this coordinate, so Core retains the returned goal between gj/gk.
        std::optional<std::size_t> preferredDisplayRowColumn;
        std::size_t topline = 0;
        std::size_t leftColumn = 0;
        std::size_t viewportRows = 0;
        std::size_t viewportColumns = 0;
        std::size_t scrollRows = 0;
        ViewportMotionAuthority viewportMotionAuthority =
            ViewportMotionAuthority::CoreLogicalRows;
        std::vector<Location> jumpList;
        std::size_t jumpIndex = 0;
        std::size_t changeIndex = 0;
        BufferId alternateBuffer = 0;
        std::optional<Location> previousContext;
    };

    struct PendingLocationMove final
    {
        WindowId window = 0;
        Location origin;
        Location destination;
        bool record = false;
        std::optional<std::size_t> jumpIndexAfterCommit;
        // Cross-buffer CTRL-O/CTRL-I must commit the cursor move and its
        // jumplist cursor atomically. The list is bounded to 100 entries, so
        // retaining this small transaction snapshot is both simpler and less
        // error-prone than mutating live history before the host binds the
        // requested buffer.
        std::optional<std::vector<Location>>
            jumpListAfterCommit;
    };

    struct PendingDisplayMotion final
    {
        DisplayMotionRequestId id = 0;
        WindowId window = 0;
        InputTargetId inputTarget = 0;
        BufferId buffer = 0;
        std::uint64_t revision = 0;
        Cursor origin;
        Command command = Command::DisplayRowDown;
        std::size_t count = 1;
        bool countWasExplicit = false;
        bool operatorPending = false;
        std::u16string repeatedInsert;
    };

    struct PromptInput final
    {
        PromptSessionId id = 0;
        WindowId window = 0;
        InputTargetId inputTarget = 0;
        std::u16string text;
    };

    struct PendingCommandLine final
    {
        WindowId window = 0;
        BufferId buffer = 0;
        CommandLineKind kind = CommandLineKind::Ex;
        std::size_t count = 1;
        bool countWasExplicit = false;
    };

    struct InputHintSession final
    {
        std::uint64_t generation = 0;
        std::string pageDownCommand;
        std::string pageUpCommand;
        // Backspace at the first trigger exposes the mapping root without
        // manufacturing typeahead. This bit distinguishes that detached
        // presentation node from a real resolver prefix.
        bool atRoot = false;
    };

    [[nodiscard]] bool promptMatches(
        const WindowId window,
        const InputTargetId inputTarget) const noexcept;

    void finishPrompt(
        DispatchResult &result,
        const EventType type);

    Implementation();

    [[nodiscard]] Mode effectiveMode() const noexcept;

    [[nodiscard]] std::size_t lineLength(
        const Buffer &buffer,
        const std::size_t line) const;

    [[nodiscard]] static std::size_t graphemeCellWidth(
        const std::u16string_view grapheme) noexcept;

    [[nodiscard]] const DisplayLine &displayLine(
        const Buffer &buffer,
        const std::size_t rawLine) const;

    [[nodiscard]] std::size_t displayColumnForBufferColumn(
        const Buffer &buffer,
        const std::size_t line,
        const std::size_t rawColumn) const;

    [[nodiscard]] DisplayPosition displayPositionForColumn(
        const Buffer &buffer,
        const std::size_t rawLine,
        const std::size_t displayColumn) const;

    [[nodiscard]] DisplayBoundary displayBoundary(
        const Buffer &buffer,
        const std::size_t line,
        const std::size_t column) const;

    [[nodiscard]] std::pair<std::size_t, std::size_t>
    displaySpan(
        const Buffer &buffer,
        const DisplayPosition &position,
        const bool blockVirtual) const;

    [[nodiscard]] DisplayPosition currentDisplayPosition(
        const View &view,
        const Buffer &buffer) const;

    void setDisplayPosition(
        View &view,
        const DisplayPosition position) const;

    [[nodiscard]] bool ownsVisualBlock(
        const WindowId window,
        const BufferId buffer) const noexcept;

    [[nodiscard]] std::optional<VisualBlockRange>
    currentVisualBlockRange(
        const WindowId window,
        const View &view,
        const Buffer &buffer) const;

    [[nodiscard]] VisualBlockRow visualBlockRow(
        const Buffer &buffer,
        const std::size_t line,
        const VisualBlockRange &range) const;

    [[nodiscard]] std::size_t visualColumnLeft(
        const DisplayLine &layout,
        std::size_t column,
        std::size_t count) const noexcept;

    [[nodiscard]] std::size_t visualColumnRight(
        const DisplayLine &layout,
        std::size_t column,
        std::size_t count) const noexcept;

    [[nodiscard]] DisplayPosition movedVisualBlockPosition(
        const Buffer &buffer,
        const DisplayPosition origin,
        const Command command,
        const std::size_t count) const;

    [[nodiscard]] Cursor normalCursorForDisplayColumn(
        const Buffer &buffer,
        const std::size_t line,
        const std::size_t displayColumn) const;

    [[nodiscard]] std::size_t normalColumnLimit(
        const Buffer &buffer,
        const std::size_t line) const;

    [[nodiscard]] std::size_t safeColumn(
        const Buffer &buffer,
        const std::size_t line,
        std::size_t column,
        const bool allowEnd) const;

    [[nodiscard]] Cursor clampCursor(
        const Buffer &buffer,
        Cursor cursor,
        const bool allowEnd) const;

    [[nodiscard]] std::size_t previousColumn(
        const Buffer &buffer,
        const std::size_t line,
        const std::size_t rawColumn) const;

    [[nodiscard]] std::size_t nextColumn(
        const Buffer &buffer,
        const std::size_t line,
        const std::size_t rawColumn) const;

    [[nodiscard]] std::size_t nextColumnAllowEnd(
        const Buffer &buffer,
        const std::size_t line,
        std::size_t column) const;

    [[nodiscard]] std::size_t previousColumnAllowEnd(
        const Buffer &buffer,
        const std::size_t line,
        std::size_t column) const;

    [[nodiscard]] std::size_t advanceWithinLine(
        const Buffer &buffer,
        const std::size_t line,
        std::size_t column,
        const std::size_t count) const;

    [[nodiscard]] std::size_t retreatWithinLine(
        const Buffer &buffer,
        const std::size_t line,
        std::size_t column,
        const std::size_t count) const;

    [[nodiscard]] std::size_t offset(
        const Buffer &buffer,
        Cursor cursor) const noexcept;

    [[nodiscard]] Cursor cursorAtOffset(
        const Buffer &buffer,
        const std::size_t rawOffset,
        const bool allowEnd = false) const;

    [[nodiscard]] std::size_t cursorCharacterEnd(
        const Buffer &buffer,
        Cursor cursor) const noexcept;

    [[nodiscard]] bool bigWordCharacter(
        const Buffer &buffer,
        const std::size_t line,
        const std::size_t column) const noexcept;

    enum class WordClass : std::uint8_t
    {
        White,
        Punctuation,
        Keyword
    };

    [[nodiscard]] WordClass wordClass(
        const Buffer &buffer,
        const std::size_t line,
        const std::size_t column,
        const bool bigWord) const noexcept;

    [[nodiscard]] std::size_t firstNonBlankColumn(
        const Buffer &buffer,
        const std::size_t line,
        const bool allowEnd = false) const noexcept;

    [[nodiscard]] std::size_t lastNonBlankColumn(
        const Buffer &buffer,
        const std::size_t line) const noexcept;

    [[nodiscard]] Cursor lastBufferCursor(
        const Buffer &buffer) const noexcept;

    [[nodiscard]] char32_t scalarAt(
        const Buffer &buffer,
        const std::size_t line,
        const std::size_t column) const noexcept;

    [[nodiscard]] std::optional<TextRange>
    textObjectRange(
        const Buffer &buffer,
        Cursor cursor,
        const char32_t object,
        const bool around,
        const std::size_t rawCount) const;

    [[nodiscard]] std::optional<Cursor>
    characterSearchTarget(
        const Buffer &buffer,
        Cursor cursor,
        const CharacterSearch &search,
        const std::size_t count,
        const bool repeated) const noexcept;

    [[nodiscard]] static std::size_t
    nextScalarOffset(
        const std::u16string &text,
        const std::size_t offset) noexcept;

    [[nodiscard]] static std::size_t
    previousScalarOffset(
        const std::u16string &text,
        std::size_t offset) noexcept;

    [[nodiscard]] std::optional<Cursor>
    matchingPairTarget(
        const Buffer &buffer,
        Cursor cursor) const noexcept;

    [[nodiscard]] std::optional<Cursor>
    nextWordStart(
        const Buffer &buffer,
        Cursor cursor,
        const bool bigWord) const noexcept;

    [[nodiscard]] std::optional<Cursor>
    previousWordStart(
        const Buffer &buffer,
        Cursor cursor,
        const bool bigWord) const noexcept;

    [[nodiscard]] std::optional<Cursor>
    nextWordEnd(
        const Buffer &buffer,
        Cursor cursor,
        const bool bigWord) const noexcept;

    [[nodiscard]] Cursor movedWordCursor(
        const Buffer &buffer,
        Cursor cursor,
        const Command command,
        const std::size_t count,
        const bool bigWord) const noexcept;

    [[nodiscard]] WordClass wordClassAtOffset(
        const Buffer &buffer,
        const std::size_t offset,
        const bool bigWord) const noexcept;

    /**
     * Implements Vim's ge/gE motion without flattening UTF-16 graphemes into
     * bytes. The current run is always skipped first; the destination is the
     * final scalar of the preceding word class (or WORD for gE).
     */
    [[nodiscard]] std::optional<Cursor> previousWordEnd(
        const Buffer &buffer,
        Cursor cursor,
        const bool bigWord) const noexcept;

    [[nodiscard]] Cursor movedWordEndBackward(
        const Buffer &buffer,
        Cursor cursor,
        const std::size_t count,
        const bool bigWord) const noexcept;

    [[nodiscard]] std::vector<std::size_t> sentenceStarts(
        const Buffer &buffer) const;

    [[nodiscard]] Cursor movedSentenceCursor(
        const Buffer &buffer,
        Cursor cursor,
        const std::size_t count,
        const bool forward) const;

    [[nodiscard]] Cursor movedParagraphCursor(
        const Buffer &buffer,
        Cursor cursor,
        const std::size_t count,
        const bool forward) const noexcept;

    [[nodiscard]] bool isSectionBoundary(
        const Buffer &buffer,
        const std::size_t line) const noexcept;

    [[nodiscard]] bool isSectionMotionTarget(
        const Buffer &buffer,
        const std::size_t line,
        const bool closingBrace) const noexcept;

    [[nodiscard]] Cursor movedSectionCursor(
        const Buffer &buffer,
        Cursor cursor,
        const std::size_t count,
        const bool forward,
        const bool closingBrace,
        const bool operatorForwardStart = false) const noexcept;

    [[nodiscard]] std::optional<std::size_t>
    wordForwardOperatorEnd(
        const Buffer &buffer,
        Cursor cursor,
        const std::size_t count,
        const bool bigWord) const noexcept;

    void emitHost(
        DispatchResult &result,
        const HostAction action,
        const std::size_t count = 1,
        const bool countWasExplicit = false) const;

    void emitModeIfChanged(
        DispatchResult &result,
        const Mode before);

    void consumeMappedHostAction(
        DispatchResult &result,
        const HostAction action);

    void consumeSemanticCommand(
        DispatchResult &result,
        std::string command,
        const InputTargetId inputTarget);

    [[nodiscard]] static bool containsHostBoundaryEvent(
        const DispatchResult &result,
        const std::size_t firstEvent) noexcept;

    void beginHostBarrier(DispatchResult &result);

    void emitCursor(
        DispatchResult &result,
        const ViewId viewId,
        const View &view) const;

    void emitViewport(
        DispatchResult &result,
        const WindowId windowId,
        const View &view) const;

    [[nodiscard]] std::vector<WindowCursorState>
    captureWindowCursors(const BufferId bufferId) const;

    void restoreWindowCursors(
        const BufferId bufferId,
        const std::vector<WindowCursorState> &state);

    void emitAttachedCursors(
        DispatchResult &result,
        const BufferId bufferId) const;

    [[nodiscard]] RegisterValue registerValue(
        const char32_t name) const;

    void writeExplicitRegister(
        const char32_t name,
        const RegisterValue &value);

    void writeYankRegister(
        std::u16string text,
        const bool linewise,
        const bool blockwise = false,
        const std::size_t blockDisplayWidth = 0);

    void writeDeleteRegister(
        std::u16string text,
        const bool linewise,
        const bool blockwise = false,
        const std::size_t blockDisplayWidth = 0);

    void writeOperationRegister(
        const OperatorKind operation,
        std::u16string text,
        const bool linewise,
        const bool blockwise = false,
        const std::size_t blockDisplayWidth = 0);

    [[nodiscard]] std::optional<Location> markLocation(
        const View &view,
        const char32_t name) const;

    void setMark(
        DispatchResult &result,
        const WindowId windowId,
        const char32_t name);

    [[nodiscard]] bool sameJumpLine(
        const Location first,
        const Location second) const;

    void appendUniqueJumpLine(
        std::vector<Location> &jumpList,
        const Location location) const;

    void recordJump(
        View &view,
        const Location origin,
        const Location destination);

    [[nodiscard]] bool moveToLocation(
        DispatchResult &result,
        const WindowId windowId,
        const Location destination,
        const bool linewise,
        const bool record,
        const std::optional<std::size_t>
            jumpIndexAfterCommit = std::nullopt);

    void jumpToMark(
        DispatchResult &result,
        const WindowId windowId,
        const char32_t name,
        const bool linewise);

    void moveInJumpList(
        DispatchResult &result,
        const WindowId windowId,
        const bool forward,
        const std::size_t count);

    void moveInChangeList(
        DispatchResult &result,
        const WindowId windowId,
        const bool forward,
        const std::size_t count);

    void requestCommandLine(
        DispatchResult &result,
        const WindowId windowId,
        const CommandLineKind kind,
        const std::size_t count,
        const bool countWasExplicit);

    void searchBuffer(
        DispatchResult &result,
        const WindowId windowId,
        const std::u16string &pattern,
        const bool forward,
        const std::size_t rawCount,
        const bool rememberPattern);

    void searchWordUnderCursor(
        DispatchResult &result,
        const WindowId windowId,
        const bool forward,
        const bool exact,
        const std::size_t count);

    struct NumberToken final
    {
        std::size_t start = 0;
        std::size_t end = 0;
        std::size_t digitsStart = 0;
        std::uint64_t value = 0;
        unsigned base = 10;
        bool negative = false;
        bool uppercasePrefix = false;
        bool uppercaseDigits = false;
    };

    [[nodiscard]] static unsigned digitValue(
        const char16_t value) noexcept;

    [[nodiscard]] static bool digitForBase(
        const char16_t value,
        const unsigned base) noexcept;

    [[nodiscard]] std::optional<NumberToken> numberAtOrAfter(
        const Buffer &buffer,
        const Cursor cursor) const;

    [[nodiscard]] static std::u16string formatUnsigned(
        std::uint64_t value,
        const unsigned base,
        const bool uppercase,
        const std::size_t minimumWidth);

    [[nodiscard]] bool changeNumber(
        DispatchResult &result,
        const WindowId windowId,
        const std::size_t count,
        const bool subtract,
        const bool remember);

    void showBufferStatus(
        DispatchResult &result,
        const WindowId windowId);

    void showDetailedBufferStatus(
        DispatchResult &result,
        const WindowId windowId);

    [[nodiscard]] bool keywordStartsWith(
        const std::u16string_view word,
        const std::u16string_view prefix) const noexcept;

    [[nodiscard]] std::optional<KeywordCompletionState>
    beginKeywordCompletion(
        const WindowId windowId,
        const Buffer &buffer,
        const View &view) const;

    [[nodiscard]] bool completeKeyword(
        DispatchResult &result,
        const WindowId windowId,
        const bool forward);

    [[nodiscard]] bool deleteInsertPrefix(
        DispatchResult &result,
        const WindowId windowId,
        const bool wholeLine);

    [[nodiscard]] bool insertRegisterValue(
        DispatchResult &result,
        const WindowId windowId,
        const char32_t name);

    [[nodiscard]] bool startMacroRecording(
        const char32_t name);

    void executeMacro(
        DispatchResult &result,
        const WindowId windowId,
        const InputTargetId inputTarget,
        const std::size_t registerIndex,
        const std::size_t rawCount);

    void abortMacroCommand() noexcept;

    void applyTextObject(
        DispatchResult &result,
        const WindowId windowId,
        const char32_t object,
        const bool around,
        const std::size_t count);

    void leaveVisualMode();

    [[nodiscard]] bool cancelStaleVisual(
        DispatchResult &result,
        const WindowId windowId);

    void activateRelativeBuffer(
        DispatchResult &result,
        const View &view,
        const ViewId viewId,
        const int direction);

    void activateAlternateBuffer(
        DispatchResult &result,
        const View &view,
        const ViewId viewId,
        const std::size_t count,
        const bool countWasExplicit);

    [[nodiscard]] Cursor movedCursor(
        View &view,
        const Command command,
        const std::size_t count,
        const bool countWasExplicit) const;

    void moveCursor(
        DispatchResult &result,
        View &view,
        const ViewId viewId,
        const Command command,
        const std::size_t count,
        const bool countWasExplicit);

    void scrollHalfPage(
        DispatchResult &result,
        View &view,
        const WindowId windowId,
        const bool forward,
        const std::size_t count,
        const bool countWasExplicit);

    void scrollFullPage(
        DispatchResult &result,
        View &view,
        const WindowId windowId,
        const bool forward,
        const std::size_t count);

    void scrollSingleLine(
        DispatchResult &result,
        View &view,
        const WindowId windowId,
        const bool forward,
        const std::size_t count);

    void positionViewport(
        DispatchResult &result,
        View &view,
        const WindowId windowId,
        const Command command);

    void scrollHorizontalViewport(
        DispatchResult &result,
        View &view,
        const WindowId windowId,
        const Command command,
        const std::size_t count,
        const bool countWasExplicit);

    void beginCharacterSearchArgument(
        const WindowId windowId,
        const std::size_t count,
        const bool countWasExplicit,
        const bool forward,
        const bool till);

    void requestDisplayMotion(
        DispatchResult &result,
        const WindowId windowId,
        const Command command,
        std::size_t count,
        const bool countWasExplicit,
        std::u16string repeatedInsert = {});

    void requestPendingOperatorDisplayMotion(
        DispatchResult &result,
        const WindowId windowId,
        const Command command,
        std::u16string repeatedInsert = {});

    void execute(
        DispatchResult &result,
        const ViewId viewId,
        const Command command);

    void beginOperator(
        DispatchResult &result,
        const ViewId viewId,
        const OperatorKind kind);

    void closeInsertUndoBlock();

    void beginInsertUndoBlock(const BufferId bufferId);

    void continueInsertUndoBlock(const WindowId windowId);

    [[nodiscard]] bool replaceOverwrite(
        DispatchResult &result,
        const WindowId windowId,
        std::u16string inserted,
        const bool recordAction);

    [[nodiscard]] bool replaceDelete(
        DispatchResult &result,
        const WindowId windowId,
        const bool recordAction);

    [[nodiscard]] bool replaceBackspace(
        DispatchResult &result,
        const WindowId windowId);

    void rememberReplaceSegment();

    void beginNewReplaceSegment(
        const WindowId windowId,
        const bool preservePreviousForDot);

    void replayReplaceActions(
        DispatchResult &result,
        const WindowId windowId,
        const std::vector<ReplaceAction> &actions,
        const std::size_t count);

    void finishReplaceMode(
        DispatchResult *const result,
        const WindowId windowId,
        const bool applyCount = true);

    void beginInsertOneNormal(
        DispatchResult &result,
        const WindowId windowId);

    void finishInsertOneNormal(
        DispatchResult &result,
        const WindowId windowId);

    void moveReplaceCursor(
        DispatchResult &result,
        const WindowId windowId,
        const SpecialKey key);

    void resetUndoHistory(
        Buffer &buffer,
        const BufferId bufferId);

    void recordChangePosition(
        Buffer &buffer,
        const BufferId bufferId,
        const std::size_t rawOffset,
        const bool startedUndoNode,
        const ViewId initiatingView);

    [[nodiscard]] std::size_t ensureUndoNode(
        Buffer &buffer,
        const bool groupWithInsert,
        std::vector<WindowCursorState> beforeCursors);

    static void patchLineStarts(
        Buffer &buffer,
        const std::size_t start,
        const std::size_t end,
        const std::u16string_view inserted);

    [[nodiscard]] bool mutateBuffer(
        DispatchResult *const result,
        const BufferId bufferId,
        const std::size_t start,
        const std::size_t end,
        std::u16string inserted,
        const std::optional<std::pair<ViewId, Cursor>>
            authoritativeCursor = std::nullopt,
        const bool recordUndo = true,
        const bool emitCursorEvents = true,
        const std::optional<AuthoritativeSelectionOffsetState>
            authoritativeSelectionOffsets = std::nullopt);

    [[nodiscard]] bool applyBufferEdit(
        DispatchResult &result,
        const BufferId bufferId,
        const std::size_t start,
        const std::size_t end,
        std::u16string inserted);

    void rememberChange(RepeatChange change);

    void beginInsertRepeat(
        RepeatChange change,
        const std::size_t baseOffset,
        const bool changedBeforeInsert);

    void recordExternalInsertEdit(
        const ViewId viewId,
        const std::size_t editOffset,
        const std::size_t removed,
        const std::u16string_view inserted);

    void finishInsertRepeat(
        DispatchResult *const result = nullptr);

    [[nodiscard]] bool rejectReadOnly(
        DispatchResult &result,
        const WindowId windowId,
        const Buffer &buffer) const;

    [[nodiscard]] bool deleteCharacters(
        DispatchResult &result,
        const WindowId windowId,
        const std::size_t count,
        const bool beforeCursor,
        const bool enterInsert,
        const Command repeatCommand);

    [[nodiscard]] bool changeToLineEnd(
        DispatchResult &result,
        const WindowId windowId,
        const bool enterInsert,
        const std::size_t count,
        const Command repeatCommand);

    [[nodiscard]] bool substituteLines(
        DispatchResult &result,
        const WindowId windowId,
        const std::size_t count,
        const Command repeatCommand);

    [[nodiscard]] bool openLine(
        DispatchResult &result,
        const WindowId windowId,
        const std::size_t count,
        const bool below,
        const Command repeatCommand);

    [[nodiscard]] bool replaceCharacters(
        DispatchResult &result,
        const WindowId windowId,
        const std::size_t count,
        const char32_t replacement,
        const bool remember);

    [[nodiscard]] bool put(
        DispatchResult &result,
        const WindowId windowId,
        const RegisterValue &payload,
        const std::size_t count,
        const bool after,
        const bool remember,
        const char32_t registerName = U'"');

    [[nodiscard]] bool joinLines(
        DispatchResult &result,
        const WindowId windowId,
        const std::size_t lineCount,
        const bool remember);

    [[nodiscard]] bool toggleCase(
        DispatchResult &result,
        const WindowId windowId,
        const std::size_t count,
        const bool remember);

    [[nodiscard]] std::u16string linewiseText(
        const Buffer &buffer,
        const std::size_t firstLine,
        const std::size_t lastLine) const;

    [[nodiscard]] std::optional<BlockInsertionEdit>
    blockInsertionEdit(
        const Buffer &buffer,
        const std::size_t line,
        const std::size_t displayColumn,
        const bool padShortLine) const;

    struct BlockRowEdit final
    {
        std::u16string selected;
        std::u16string replacement;
        std::size_t insertionColumn = 0;
    };

    [[nodiscard]] BlockRowEdit blockRowEdit(
        const Buffer &buffer,
        const std::size_t line,
        const std::size_t firstDisplayColumn,
        const std::size_t lastDisplayColumnExclusive) const;

    [[nodiscard]] bool beginVisualBlockInsert(
        DispatchResult &result,
        const WindowId windowId,
        const bool append);

    [[nodiscard]] bool swapVisualBlockCorner(
        DispatchResult &result,
        const WindowId windowId,
        const bool horizontalOnly);

    [[nodiscard]] static std::vector<std::u16string_view>
    blockRegisterRows(const std::u16string &text);

    [[nodiscard]] bool putOverVisualBlock(
        DispatchResult &result,
        const WindowId windowId,
        const RegisterValue &payload,
        const Command command);

    [[nodiscard]] bool replaceVisualBlock(
        DispatchResult &result,
        const WindowId windowId,
        const char32_t replacementCharacter,
        const bool remember);

    [[nodiscard]] bool toggleVisualBlock(
        DispatchResult &result,
        const WindowId windowId,
        const bool remember);

    [[nodiscard]] bool finishVisualTransform(
        DispatchResult &result,
        const WindowId windowId,
        const OperatorKind operation,
        const std::size_t rawCount,
        const bool remember);

    void finishVisualOperation(
        DispatchResult &result,
        const WindowId windowId,
        const OperatorKind operation);

    [[nodiscard]] bool replayUndoNode(
        DispatchResult &result,
        const BufferId bufferId,
        const std::size_t nodeIndex,
        const bool redo);

    void undoOrRedo(
        DispatchResult &result,
        const WindowId windowId,
        const std::size_t count,
        const bool redo);

    [[nodiscard]] DispatchResult replayPublicHistory(
        const WindowId window,
        const std::size_t count,
        const bool redo);

    [[nodiscard]] static constexpr bool transformOperator(
        const OperatorKind kind) noexcept;

    [[nodiscard]] std::u16string shiftedLines(
        const Buffer &buffer,
        const std::size_t firstLine,
        const std::size_t lastLine,
        const bool right,
        const std::size_t levels = 1) const;

    [[nodiscard]] std::u16string transformedOperatorText(
        const Buffer &buffer,
        const OperatorKind operation,
        const std::size_t start,
        const std::size_t end,
        std::size_t *const normalizedStart,
        std::size_t *const normalizedEnd,
        std::size_t *const firstLine) const;

    void finishOperator(
        DispatchResult &result,
        const ViewId viewId,
        std::size_t rangeStart,
        std::size_t rangeEnd,
        const bool linewise,
        std::u16string registerText = {},
        std::optional<RepeatChange> repeat =
            std::nullopt);

    void finishLinewiseOperator(
        DispatchResult &result,
        const ViewId viewId);

    void cancelPendingOperatorMotion(
        DispatchResult &result);

    void finishCharacterwiseOperator(
        DispatchResult &result,
        const WindowId windowId,
        const Cursor origin,
        const Cursor target,
        const bool inclusive,
        std::optional<RepeatChange> repeat =
            std::nullopt,
        const std::optional<std::size_t>
            forwardEndOverride = std::nullopt);

    void applyResolvedDisplayMotion(
        DispatchResult &result,
        const PendingDisplayMotion &transaction,
        const DisplayMotionResolution &resolution);

    void finishCharacterSearchMotion(
        DispatchResult &result,
        const WindowId windowId,
        CharacterSearch search,
        const std::size_t count,
        const bool repeated);

    void applyRepeatedCharacterSearch(
        DispatchResult &result,
        const WindowId windowId,
        const std::size_t count,
        const bool opposite);

    void applyMatchingPairMotion(
        DispatchResult &result,
        const WindowId windowId);

    void finishMotionOperator(
        DispatchResult &result,
        const ViewId viewId,
        const Command motion);

    void completeRepeatedInsert(
        DispatchResult &result,
        const WindowId windowId,
        const std::u16string &inserted,
        const bool closeUndo = true);

    void appendRepeatedOpenLines(
        DispatchResult &result,
        const WindowId windowId,
        const std::u16string &inserted,
        const std::size_t copies);

    void repeatVisualBlock(
        DispatchResult &result,
        const WindowId windowId,
        const RepeatChange &repeat,
        const std::size_t commandCount,
        const bool countWasExplicit);

    void repeatVisualBlockInsert(
        DispatchResult &result,
        const WindowId windowId,
        const RepeatChange &repeat);

    void repeatVisualBlockShape(
        DispatchResult &result,
        const WindowId windowId,
        const RepeatChange &repeat);

    void repeatVisualCharacter(
        DispatchResult &result,
        const WindowId windowId,
        const RepeatChange &repeat);

    void repeatVisualTransform(
        DispatchResult &result,
        const WindowId windowId,
        const RepeatChange &repeat);

    void repeatLastChange(
        DispatchResult &result,
        const WindowId windowId,
        const std::size_t commandCount,
        const bool countWasExplicit);

    [[nodiscard]] bool consumePendingArgument(
        DispatchResult &result,
        const WindowId windowId,
        const KeyAtom &key);

    void consumeNormalKey(
        DispatchResult &result,
        const ViewId viewId,
        const InputTargetId inputTarget,
        const KeyAtom &key);

    [[nodiscard]] bool nativeGrammarPending() const noexcept;

    [[nodiscard]] std::u32string
    pendingOperatorNotation() const;

    static void appendNativeHint(
        InputHintSnapshot &snapshot,
        const NativeHintDescriptor &descriptor,
        const std::string_view descriptionOverride = {},
        const std::string_view groupOverride = {});

    template<typename Descriptor, std::size_t Size>
    static void appendNativeHints(
        InputHintSnapshot &snapshot,
        const std::array<Descriptor, Size> &descriptors);

    [[nodiscard]] std::optional<InputHintSnapshot>
    nativeGrammarSnapshot(
        const std::uint64_t generation) const;

    /**
     * Adds the native node that would receive a mapping-prefix mismatch.
     *
     * Neovim resolves mappings before its built-in command grammar. A local
     * `gg` mapping therefore owns the `g` child, while the native `g_` branch
     * remains reachable when `_` makes the mapping trie miss. Preserve the
     * mapping snapshot's wait policy and locality metadata, append only the
     * unshadowed native children, and never mutate the live grammar state.
     */
    void mergeNativeFallbackHints(
        InputHintSnapshot &snapshot) const;

    [[nodiscard]] bool inputHintSessionIsLive() const noexcept;

    /**
     * Drops the presentation lease without resolving real typeahead.
     *
     * A root node reached through Backspace is the sole exception: it is a
     * detached view of the mapping trie, so it has no resolver transaction to
     * preserve after the UI closes.
     */
    void endInputHintPresentation();

    /** Moves one native grammar level towards its parent. */
    void retreatNativeHintPrefix();

    [[nodiscard]] std::optional<DispatchResult>
    consumeInputHintSessionKey(
        const ViewId view,
        const InputTargetId inputTarget,
        const KeyAtom &key);

    [[nodiscard]] DispatchResult resolveTypeahead(
        const ViewId viewId,
        const InputTargetId inputTarget,
        const bool timedOut);

    void clearPendingState();

    [[nodiscard]] bool bufferAttached(
        const BufferId buffer) const noexcept;

    std::size_t eraseBuffers(
        const std::unordered_set<BufferId> &removed);

    mutable detail::CanonicalKeyEventCache
        canonicalEvents;
    detail::InputEngine input;
    VkUserCommandRegistry userCommands;
    bool enabled = true;
    Mode baseMode = Mode::Normal;
    VirtualEditMode virtualEdit = VirtualEditMode::None;
    ViewId nextView = 1;
    BufferId nextBuffer = 1;
    ViewId activeView = 0;
    ViewId previousActiveView = 0;
    std::unordered_map<ViewId, View> views;
    std::unordered_map<BufferId, Buffer> buffers;
    std::unordered_map<
        std::string,
        BufferId,
        StringViewHash,
        std::equal_to<>>
        paths;
    std::vector<BufferId> bufferOrder;
    RegisterValue unnamedRegister;
    std::array<RegisterValue, 10> numberedRegisters;
    std::array<RegisterValue, 26> namedRegisters;
    RegisterValue smallDeleteRegister;
    std::array<std::optional<Location>, 26> globalMarks;
    std::optional<std::size_t> recordingMacro;
    std::optional<std::size_t> lastPlayedMacro;
    bool macroPlaybackActive = false;
    std::size_t macroStepsRemaining = 0;
    MacroCommandDisposition macroCommandDisposition =
        MacroCommandDisposition::Continue;
    char32_t selectedRegister = U'"';
    CommandPrefix prefix = CommandPrefix::None;
    std::optional<PendingOperator> pendingOperator;
    std::optional<PendingArgument> pendingArgument;
    std::optional<PendingCommandLine> pendingCommandLine;
    std::optional<CharacterSearch> lastCharacterSearch;
    std::u16string lastSearchPattern;
    bool lastSearchForward = true;
    std::u16string cachedSearchPattern;
    QRegularExpression cachedSearchExpression;
    bool searchExpressionCached = false;
    std::optional<VisualSelection> visualSelection;
    std::optional<RepeatChange> lastChange;
    std::optional<RepeatChange> activeInsertRepeat;
    std::optional<BlockInsertState> activeBlockInsert;
    std::optional<ReplaceSession> replaceSession;
    std::optional<KeywordCompletionState> keywordCompletion;
    std::size_t insertRepeatBaseOffset = 0;
    bool activeInsertRepeatValid = false;
    bool insertOneNormalPending = false;
    bool insertOneNormalSawKey = false;
    bool replayingChange = false;
    std::optional<BufferId> insertUndoBuffer;
    std::size_t normalCount = 0;
    std::uint64_t inputGeneration = 0;
    std::optional<std::uint64_t> pendingGeneration;
    std::optional<InputHintSession> inputHintSession;
    std::optional<InputHintWaitPolicy>
        pendingHintPolicy;
    ViewId pendingView = 0;
    InputTargetId pendingTarget = 0;
    std::uint64_t hostBarrierGeneration = 0;
    std::optional<std::uint64_t>
        pendingHostBarrierGeneration;
    std::optional<PendingLocationMove> pendingLocationMove;
    DisplayMotionRequestId nextDisplayMotionRequest = 1;
    std::optional<PendingDisplayMotion> pendingDisplayMotion;
    ViewId inputContextView = 0;
    InputTargetId inputContextTarget = 0;
    PromptSessionId nextPromptSession = 1;
    std::optional<PromptInput> promptInput;
    int timeoutMilliseconds = 1'000;
};


} // namespace vkui::vk
