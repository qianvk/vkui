#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vkui::vk {

using BufferId = std::uint64_t;
using WindowId = std::uint64_t;
using ViewId = WindowId;
using InputTargetId = std::uint64_t;
using PromptSessionId = std::uint64_t;
using DisplayMotionRequestId = std::uint64_t;

enum class Mode : std::uint8_t
{
    Normal,
    Insert,
    Replace,
    Visual,
    OperatorPending
};

/**
 * Shape of the selection owned by Visual mode.
 *
 * Keeping this in Core (instead of inferring it from Qt selections) lets the
 * same operator engine serve text editors and structured line surfaces.
 */
enum class VisualKind : std::uint8_t
{
    Character,
    Line,
    Block
};

/**
 * Typed subset of Vim's 'virtualedit' option supported by VkCore.
 *
 * None matches Neovim's default: Visual Block may stop on the one-past-EOL
 * cell but cannot travel farther. Block preserves an exact screen column
 * inside tabs and beyond short lines while Visual Block is active.
 */
enum class VirtualEditMode : std::uint8_t
{
    None,
    Block
};

enum class ViewKind : std::uint8_t
{
    Other,
    Navigation,
    Editor,
    Surface
};

// Window is the canonical VK term. ViewKind remains as a source-compatible
// spelling while the Qt host migrates from its original view-oriented API.
using WindowKind = ViewKind;

/**
 * Semantic data role of a buffer.
 *
 * BufferKind deliberately describes the core data source rather than the Qt
 * widget that happens to present it. Core plugins can therefore lazy-load
 * from a resolved buffer transition without depending on host key events or
 * UI positions.
 */
enum class BufferKind : std::uint8_t
{
    Other,
    Text,
    Navigation,
    Reader,
    Settings
};

struct Cursor final
{
    // UTF-16 code-unit offset inside the logical buffer line. This is never a
    // terminal/display column; Qt uses the same coordinate at the host edge.
    std::size_t line = 0;
    std::size_t column = 0;

    friend bool operator==(const Cursor &,
                           const Cursor &) = default;
};

/**
 * A buffer position paired with its zero-based display-cell column.
 *
 * Visual Block mode may place the display column inside a tab expansion or
 * beyond a short line while the buffer cursor remains at the tab or EOL.
 * Keeping both values prevents lossy UTF-16 <-> screen-column round trips.
 */
struct DisplayPosition final
{
    Cursor buffer;
    std::size_t column = 0;

    friend bool operator==(
        const DisplayPosition &,
        const DisplayPosition &) = default;
};

/** Target semantics for absolute display-cell to UTF-16 cursor resolution. */
enum class DisplayColumnMode : std::uint8_t
{
    /** Canonical grapheme start, clamped to the last character. */
    Character,
    /** Exact tab/virtual cell used by Visual Block geometry. */
    VisualBlock
};

/**
 * A motion whose destination depends on the host's wrapped-row geometry.
 *
 * Core owns the grammar and transaction; the renderer only measures the
 * requested destination. This keeps gj/gk/g0/g^/g$/gm faithful without
 * pretending that a logical buffer line is one display row.
 */
enum class DisplayMotionKind : std::uint8_t
{
    RowUp,
    RowDown,
    RowStart,
    RowFirstNonBlank,
    RowEnd,
    RowMiddle
};

/** Host-measured result for one DisplayMotionRequested transaction. */
struct DisplayMotionResolution final
{
    DisplayPosition destination;
    /**
     * Zero-based preferred display cell relative to the wrapped row's start,
     * retained as Neovim's vertical curswant for gj/gk repetition.
     * Horizontal display-row motions leave this empty.
     */
    std::optional<std::size_t> rowGoalColumn;

    friend bool operator==(
        const DisplayMotionResolution &,
        const DisplayMotionResolution &) = default;
};

/** Inclusive line range and half-open display-cell columns of a block. */
struct VisualBlockRange final
{
    std::size_t firstLine = 0;
    std::size_t lastLine = 0;
    std::size_t firstColumn = 0;
    std::size_t lastColumnExclusive = 1;

    friend bool operator==(
        const VisualBlockRange &,
        const VisualBlockRange &) = default;
};

/**
 * One display boundary resolved back to a UTF-16 line position.
 *
 * When displayColumn lies inside a tab, bufferColumn identifies the tab and
 * cell* describes its full expansion. Beyond EOL, bufferColumn and
 * cellBufferEnd are both the line length and cellDisplayStart is the rendered
 * line width. The Qt bridge can therefore interpolate a precise pixel edge.
 */
struct DisplayBoundary final
{
    std::size_t displayColumn = 0;
    std::size_t bufferColumn = 0;
    std::size_t cellBufferEnd = 0;
    std::size_t cellDisplayStart = 0;
    std::size_t cellDisplayEnd = 0;

    friend bool operator==(
        const DisplayBoundary &,
        const DisplayBoundary &) = default;
};

/** Viewport-friendly projection data for one Visual Block row. */
struct VisualBlockRow final
{
    std::size_t line = 0;
    DisplayBoundary left;
    DisplayBoundary right;
    std::size_t selectedBufferStart = 0;
    std::size_t selectedBufferEnd = 0;

    friend bool operator==(
        const VisualBlockRow &,
        const VisualBlockRow &) = default;
};

/** Lightweight diagnostics for the lazy display-layout cache. */
struct DisplayLayoutCacheStats final
{
    std::size_t cachedLines = 0;
    std::size_t specialCells = 0;
    std::size_t allocatedCellCapacity = 0;

    friend bool operator==(
        const DisplayLayoutCacheStats &,
        const DisplayLayoutCacheStats &) = default;
};

/**
 * Selects who measures viewport-relative motions such as Ctrl-D/Ctrl-U.
 *
 * Ordinary text windows use logical buffer rows. Structured renderers whose
 * visual rows do not correspond one-to-one with buffer lines can opt into a
 * host measurement transaction while VkCore keeps ownership of the grammar
 * and of the resulting cursor.
 */
enum class ViewportMotionAuthority : std::uint8_t
{
    CoreLogicalRows,
    HostVisual
};

/**
 * Immutable public state for one core window.
 *
 * A window is a viewport onto a buffer. Multiple windows may therefore share
 * one BufferId while retaining independent cursor and viewport state.
 */
struct WindowSnapshot final
{
    WindowId id = 0;
    ViewKind kind = ViewKind::Other;
    BufferId buffer = 0;
    Cursor cursor;
    std::size_t topline = 0;
    std::size_t leftColumn = 0;
    std::size_t viewportRows = 0;
    std::size_t viewportColumns = 0;
    std::size_t scrollRows = 0;
    ViewportMotionAuthority viewportMotionAuthority =
        ViewportMotionAuthority::CoreLogicalRows;
    /**
     * Inclusive Visual-mode anchor owned by this exact window/buffer pair.
     *
     * The field is empty for every other window, after the window changes
     * buffers, and outside Visual mode. This prevents a presentation layer
     * from projecting a stale selection onto an unrelated text surface.
     */
    std::optional<Cursor> visualAnchor;
    bool visualLinewise = false;
    bool visualBlockwise = false;
    DisplayPosition displayCursor;
    std::optional<DisplayPosition> visualDisplayAnchor;
    std::optional<VisualBlockRange> visualBlock;
    std::size_t tabStop = 8;
};

enum class HostAction : std::uint8_t
{
    None,
    FocusPanelLeft,
    FocusPanelDown,
    FocusPanelUp,
    FocusPanelRight,
    ShowPreferences,
    NavigateLeft,
    NavigateDown,
    NavigateUp,
    NavigateRight,
    NavigateHalfPageDown,
    NavigateHalfPageUp,
    ScrollViewportLineDown,
    ScrollViewportLineUp,
    ScrollViewportLeft,
    ScrollViewportRight,
    ScrollViewportHalfLeft,
    ScrollViewportHalfRight,
    PositionViewportCursorTop,
    PositionViewportCursorCenter,
    PositionViewportCursorBottom,
    PositionViewportCursorStart,
    PositionViewportCursorEnd,
    NavigateFirst,
    NavigateLast,
    Activate,
    Cancel
};

enum class EventType : std::uint8_t {
    HostAction,
    ModeChanged,
    BufferActivationRequested,
    CursorChanged,
    ViewportChanged,
    BufferEdited,
    /** External authority committed, but Core cannot obtain its post-commit snapshot. */
    ExternalAuthorityDesynchronized,
    InsertText,
    InsertCommand,
    InputError,
    /**
     * Stable semantic command resolved by the Core Plugin command registry.
     *
     * Like HostAction, this is a host transaction boundary: a command may
     * mount, hide, move, or focus a panel before remaining typeahead resumes.
     */
    CommandRequested,
    CommandLineRequested,
    /** A Core-owned transient text prompt was accepted by the user. */
    PromptSubmitted,
    /** A Core-owned transient text prompt was cancelled or lost context. */
    PromptCancelled,
    /** Informational text produced by a native command such as Ctrl-G. */
    StatusMessage,
    /** Renderer measurement request for a wrapped display-row motion. */
    DisplayMotionRequested
};

enum class CommandLineKind : std::uint8_t
{
    Ex,
    SearchForward,
    SearchBackward
};

enum class InsertCommand : std::uint8_t
{
    Backspace,
    Delete,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    PageUp,
    PageDown,
    MoveToStart,
    MoveToEnd,
    Copy,
    Cut,
    Paste,
    SelectAll
};

struct CommandLineRange final
{
    std::size_t firstLine = 0;
    std::size_t lastLine = 0;

    friend bool operator==(
        const CommandLineRange &,
        const CommandLineRange &) = default;
};

struct Event final
{
    EventType type = EventType::HostAction;
    HostAction hostAction = HostAction::None;
    ViewId view = 0;
    InputTargetId inputTarget = 0;
    BufferId buffer = 0;
    Cursor cursor;
    bool hasCursor = false;
    std::size_t topline = 0;
    std::size_t leftColumn = 0;
    std::size_t viewportRows = 0;
    std::size_t viewportColumns = 0;
    std::size_t scrollRows = 0;
    Mode mode = Mode::Normal;
    std::size_t editOffset = 0;
    std::size_t editRemoved = 0;
    std::u16string editInserted;
    std::uint64_t authorityRevision = 0;
    std::size_t authoritySize = 0;
    InsertCommand insertCommand = InsertCommand::Backspace;
    std::string message;
    std::string commandId;
    std::vector<std::string> commandArguments;
    CommandLineKind commandLineKind = CommandLineKind::Ex;
    PromptSessionId promptSession = 0;
    std::u16string promptText;
    // Context captured before a mapped command clears Normal grammar state.
    // Consumers must not reconstruct these values from the later active UI.
    std::size_t count = 1;
    bool countWasExplicit = false;
    bool bang = false;
    std::optional<CommandLineRange> lineRange;
    std::string rawArguments;
    DisplayMotionRequestId displayMotionRequest = 0;
    std::uint64_t displayBufferRevision = 0;
    DisplayMotionKind displayMotionKind =
        DisplayMotionKind::RowDown;
    // Absolute display cell in the complete logical line. The renderer maps
    // it into one QTextLine; it never recomputes tabs or Unicode cell widths.
    DisplayPosition displayPosition;
    // Zero-based wrapped-row-relative vertical goal, when already known.
    std::optional<std::size_t> displayRowGoalColumn;
};

enum class InputDisposition : std::uint8_t
{
    PassThrough,
    Consumed,
    Pending
};

struct DispatchResult final
{
    InputDisposition disposition = InputDisposition::PassThrough;
    std::vector<Event> events;
    std::optional<std::uint64_t> mappingDeadlineGeneration;
    std::optional<InputTargetId> mappingInputTarget;
    /**
     * Identifies a host transaction that must be acknowledged before VK may
     * continue consuming the current typeahead transaction.
     *
     * Host actions can synchronously change the active view, input target, or
     * buffer. Keeping the remaining mapping RHS behind this barrier prevents
     * it from executing against stale core state.
     */
    std::optional<std::uint64_t> hostBarrierGeneration;
    int mappingTimeoutMilliseconds = 0;
    /**
     * Generation of the complete Core-owned input hint state.
     *
     * Unlike mappingDeadlineGeneration this is also populated for prefixes
     * that intentionally wait forever, such as native Normal grammar and a
     * Leader mapping session. Hosts must only start timeoutlen for
     * TimedMapping; every other policy is dismissed by a subsequent key,
     * Escape, or an explicit input-context transition.
     */
    std::optional<std::uint64_t> inputHintGeneration;
};

using MappingId = std::uint64_t;

enum class MappingMode : std::uint8_t
{
    Normal = 1U << 0U,
    Insert = 1U << 1U,
    Visual = 1U << 2U,
    OperatorPending = 1U << 3U
};

using MappingModes = std::uint8_t;

[[nodiscard]] constexpr MappingModes mappingModes(
    const MappingMode mode) noexcept
{
    return static_cast<MappingModes>(mode);
}

[[nodiscard]] constexpr MappingModes operator|(
    const MappingMode lhs,
    const MappingMode rhs) noexcept
{
    return static_cast<MappingModes>(
        static_cast<MappingModes>(lhs)
        | static_cast<MappingModes>(rhs));
}

[[nodiscard]] constexpr MappingModes operator|(
    const MappingModes lhs,
    const MappingMode rhs) noexcept
{
    return static_cast<MappingModes>(
        lhs | static_cast<MappingModes>(rhs));
}

[[nodiscard]] constexpr MappingModes operator|(
    const MappingMode lhs,
    const MappingModes rhs) noexcept
{
    return static_cast<MappingModes>(
        static_cast<MappingModes>(lhs) | rhs);
}

enum class RemapPolicy : std::uint8_t
{
    Recursive,
    NoRemap
};

struct KeySequenceTarget final
{
    // Key notation is parsed once when the mapping is defined.
    std::u32string notation;
};

struct HostActionTarget final
{
    HostAction action = HostAction::None;
};

struct CommandTarget final
{
    // Stable semantic id resolved by the core command registry.
    std::string id;
};

using MappingTarget =
    std::variant<
        KeySequenceTarget,
        HostActionTarget,
        CommandTarget>;

struct MappingOptions final
{
    RemapPolicy remap = RemapPolicy::Recursive;
    bool nowait = false;
    bool silent = false;
    /**
     * Retains every lhs key except the final selector after a command runs.
     *
     * This is a declarative Vim-style submode primitive: mappings which share
     * a prefix can be repeated with only their final key until Escape or an
     * unrelated key leaves the prefix. It intentionally does not modify the
     * dot-repeat transaction, which remains reserved for buffer changes.
     */
    bool repeatPrefix = false;
};

/**
 * Presentation metadata shared by the resolver and which-key UI.
 *
 * It is stored beside the executable mapping so discovery never needs a
 * second registration table that can become stale.
 */
struct MappingMetadata final
{
    std::string description;
    std::string group;
    std::string sourcePlugin;
    bool hidden = false;
    int order = 0;
};

struct MappingDefinition final
{
    MappingModes modes = mappingModes(MappingMode::Normal);
    std::u32string lhs;
    MappingTarget target;
    MappingOptions options;
    std::optional<BufferId> buffer;
    // Trailing field preserves every existing aggregate initializer.
    MappingMetadata metadata{};
    /**
     * Optional window-local scope.
     *
     * A Surface does not necessarily own a text buffer. Window locality keeps
     * contextual panel keymaps in the same resolver, with precedence
     * window-local > buffer-local > global.
     */
    std::optional<WindowId> window;
};

/** Result of one atomic mapping-owner revocation transaction. */
struct MappingRevocation final
{
    std::size_t removed = 0;
    /** Previous pending generation invalidated by this exact revocation. */
    std::optional<std::uint64_t> invalidatedInputGeneration;
};

enum class InputHintOrigin : std::uint8_t
{
    Mapping,
    NativeGrammar
};

enum class InputHintWaitPolicy : std::uint8_t
{
    /**
     * An ordinary ambiguous mapping. Resolve it after timeoutlen unless an
     * exact <nowait> mapping wins first.
     */
    TimedMapping,
    /**
     * A mapping rooted at Leader/LocalLeader. The explicit Leader session is
     * never converted to a literal key merely because wall-clock time passed.
     */
    PersistentLeader,
    /**
     * Native Normal grammar such as g, Ctrl-W, z, or an operator. Neovim gets
     * the next command key synchronously and does not apply timeoutlen here.
     */
    PersistentGrammar
};

/**
 * One directly reachable child of the resolver's current mapping node.
 *
 * Only stable value types cross the VkCore boundary. Internal key atoms stay
 * private to the resolver; canonical notation is suitable for UI display and
 * for correlating a selected hint with later logical input.
 */
struct MappingPrefixCandidate final
{
    std::u32string keyNotation;
    std::u32string sequenceNotation;
    MappingId mappingId = 0;
    bool completesMapping = false;
    bool hasChildren = false;
    bool bufferLocal = false;
    bool windowLocal = false;
    InputHintOrigin origin = InputHintOrigin::Mapping;
    std::string description;
    std::string group;
    // Stable owner id of the contributing plugin.
    std::string sourcePlugin;
    bool hidden = false;
    int order = 0;
    std::string commandId;
};

/**
 * Detached immutable-value snapshot for a pending mapping prefix.
 *
 * generation is supplied by the caller's mapping deadline transaction, so a
 * delayed floating panel can discard stale data without reaching into the
 * mutable input engine.
 */
struct MappingPrefixSnapshot final
{
    MappingMode mode = MappingMode::Normal;
    std::optional<BufferId> buffer;
    std::optional<WindowId> window;
    std::uint64_t generation = 0;
    InputHintWaitPolicy waitPolicy =
        InputHintWaitPolicy::TimedMapping;
    std::u32string prefixNotation;
    std::vector<MappingPrefixCandidate> candidates;
    /**
     * True when a mismatch at this mapping node emits the literal pending
     * prefix into native grammar.
     *
     * An exact short mapping waiting beside longer mappings consumes the
     * prefix on mismatch instead, so native alternatives must not be
     * advertised by input-hint UIs in that case.
     */
    bool nativeFallbackReachable = false;
};

// The snapshot now covers both mappings and native grammar. Keep the original
// names source-compatible while new host code uses the semantically complete
// InputHint spelling.
using InputHintCandidate = MappingPrefixCandidate;
using InputHintSnapshot = MappingPrefixSnapshot;

struct BufferSnapshot final
{
    BufferId id = 0;
    std::string path;
    std::u16string text;
    std::uint64_t revision = 0;
};

/** Immutable state of one authoritative buffer's branching edit history. */
struct BufferHistorySnapshot final
{
    BufferId id = 0;
    std::uint64_t current = 0;
    std::optional<std::uint64_t> clean;
    bool canUndo = false;
    bool canRedo = false;

    [[nodiscard]] bool modified() const noexcept
    {
        return !clean || *clean != current;
    }
};

/** Lightweight ownership/memory facts for host session eviction policy. */
struct BufferAuthoritySnapshot final {
    BufferId id = 0;
    bool externalTextAuthority = false;
    bool externalHistoryAuthority = false;
    std::size_t residentTextCodeUnits = 0;
    std::size_t cachedTextCodeUnits = 0;
    std::size_t residentLineIndexEntries = 0;
    std::size_t residentUndoNodes = 0;
    bool desynchronized = false;
    std::uint64_t authorityRevision = 0;
    std::size_t authoritySize = 0;
};

/**
 * Immutable bounded read from one authoritative buffer revision.
 *
 * Renderers and plugins should retain offsets/revisions rather than a second
 * whole-document copy. A later read whose revision differs invalidates any
 * cached layout derived from the earlier slice.
 */
struct BufferRangeSnapshot final
{
    BufferId id = 0;
    std::uint64_t revision = 0;
    std::size_t offset = 0;
    std::size_t totalSize = 0;
    std::u16string text;

    [[nodiscard]] bool atEnd() const noexcept
    {
        return offset + text.size() >= totalSize;
    }
};

} // namespace vkui::vk
