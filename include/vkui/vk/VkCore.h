#pragma once

#include "VkTypes.h"
#include "VkUserCommandRegistry.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vkui/buffer/BufferStorage.h>

class QKeyEvent;

namespace vkui::vk {

enum class BufferRegistrationStatus : std::uint8_t {
    Created,
    AlreadyRegistered,
    InvalidPath,
    InvalidProvider,
    UnsupportedLineEndings,
    PathConflict,
};

struct BufferRegistrationResult final
{
    BufferId buffer = 0;
    BufferRegistrationStatus status =
        BufferRegistrationStatus::InvalidProvider;

    [[nodiscard]] bool registered() const noexcept
    {
        return status == BufferRegistrationStatus::Created
            || status
                == BufferRegistrationStatus::AlreadyRegistered;
    }
};

enum class BufferAttachStatus : std::uint8_t
{
    Attached,
    UnknownWindow,
    UnknownBuffer,
    RequiresResidentTextProjection,
};

/**
 * Widget-independent modal editing and workspace data core.
 *
 * VkCore never references a QWidget or mirrors Qt key enums. The application
 * passes the original QKeyEvent at the ingress boundary; the core performs
 * the one required canonicalization and then owns stable buffer/view/input
 * target identities, per-view cursors, modal state, typeahead and actions.
 */
class VkCore final
{
public:
    VkCore();
    ~VkCore();

    VkCore(const VkCore &) = delete;
    VkCore &operator=(const VkCore &) = delete;

    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const noexcept;
    [[nodiscard]] std::optional<Mode> mode() const noexcept;
    void setVirtualEditMode(VirtualEditMode mode);
    [[nodiscard]] VirtualEditMode virtualEditMode() const noexcept;

    [[nodiscard]] ViewId registerView(ViewKind kind);
    void unregisterView(ViewId view);
    [[nodiscard]] bool setActiveView(ViewId view);
    [[nodiscard]] ViewId activeView() const noexcept;
    [[nodiscard]] ViewKind viewKind(ViewId view) const noexcept;
    /**
     * Canonical window API.
     *
     * A VK window is a viewport onto a buffer, matching Neovim's
     * buffer/window split. The view-named functions above remain compatibility
     * wrappers for existing host code; new panel hosts should use these names.
     */
    [[nodiscard]] WindowId registerWindow(WindowKind kind)
    {
        return registerView(kind);
    }
    void unregisterWindow(const WindowId window)
    {
        unregisterView(window);
    }
    [[nodiscard]] bool setActiveWindow(
        const WindowId window)
    {
        return setActiveView(window);
    }
    [[nodiscard]] WindowId activeWindow() const noexcept
    {
        return activeView();
    }
    /** Last distinct live window selected before activeWindow(). */
    [[nodiscard]] WindowId previousWindow() const noexcept;
    [[nodiscard]] WindowKind windowKind(
        const WindowId window) const noexcept
    {
        return viewKind(window);
    }
    /**
     * Updates host-measured viewport state for one core window.
     *
     * topline/viewportRows remain logical buffer rows for page scrolling. A
     * zero scrollRows value makes half-page commands derive their distance
     * from viewportRows; an explicit count updates scrollRows like Neovim's
     * window-local 'scroll' option. viewportColumns gives zH/zL and zs/ze a
     * host-measured display-cell width rather than a guessed pixel mapping.
     * Wrapped-row motions use the separate
     * DisplayMotionRequested transaction, so these coarse viewport values are
     * never mistaken for renderer layout. Omitting scrollRows preserves the
     * option; explicitly passing zero restores automatic half-page scrolling.
     */
    [[nodiscard]] bool setWindowViewport(
        WindowId window,
        std::size_t topline,
        std::size_t leftColumn,
        std::size_t viewportRows,
        std::optional<std::size_t> scrollRows =
            std::nullopt,
        std::optional<std::size_t> viewportColumns =
            std::nullopt);
    /** Changes only the projection authority for viewport-relative motions. */
    [[nodiscard]] bool setWindowViewportMotionAuthority(
        WindowId window,
        ViewportMotionAuthority authority);
    [[nodiscard]] std::optional<WindowSnapshot> window(
        WindowId window) const;
    /**
     * Copies per-window navigation history when the host splits a window.
     *
     * Buffers and cursors remain authoritative through attachBuffer(); this
     * narrow operation only mirrors Neovim's rule that a newly split window
     * starts with the source window's jumplist, changelist cursor, alternate
     * buffer, and previous-context mark.
     */
    [[nodiscard]] bool cloneWindowNavigationState(
        WindowId source,
        WindowId target);
    /**
     * Resolves only the requested Visual Block rows for a renderer.
     *
     * Columns are display cells; buffer columns are UTF-16 offsets. Callers
     * should bound firstLine/lastLine to their viewport for large selections.
     */
    [[nodiscard]] std::vector<VisualBlockRow> visualBlockRows(
        WindowId window,
        std::size_t firstLine,
        std::size_t lastLine) const;

    [[nodiscard]] BufferId synchronizeBuffer(
        ViewId view,
        std::string path,
        std::u16string text,
        Cursor cursor);
    /**
     * Registers a non-blocking paged source without materializing its text.
     *
     * Provider storage is immediately available through the descriptor/range
     * APIs. Native text motions remain gated until a generic resident address
     * projection is supplied; attachBufferData() reports that capability
     * boundary explicitly instead of dereferencing provider storage.
     */
    [[nodiscard]] BufferRegistrationResult registerProviderBuffer(
        std::string path,
        std::shared_ptr<const vkui::buffer::IRangeProvider> provider,
        std::size_t maximumReadLength =
            vkui::buffer::BufferStorage::defaultMaximumReadLength);
    /**
     * Registers a host-owned editable document without importing its text.
     *
     * The external session remains the sole text and history authority.
     * VkCore retains one immutable snapshot handle and a fixed-size scalar
     * cache; it owns no full UTF-16 mirror, line-start vector, or undo graph
     * for this buffer.
     *
     * Modal mutation currently requires the snapshot descriptor's O(1)
     * modalEditingLfOnly capability. Registration rejects unverified,
     * CR/CRLF, or mixed-line-ending sources, and every committed replacement
     * snapshot is checked again before Core adopts it.
     */
    [[nodiscard]] BufferRegistrationResult registerExternalSessionBuffer(
        std::string path, std::shared_ptr<vkui::buffer::IEditableTextSession> session,
        std::size_t maximumReadLength = vkui::buffer::BufferStorage::defaultMaximumReadLength,
        std::size_t scalarCacheCapacity = 4096);
    /**
     * Binds an existing authoritative buffer to one window.
     *
     * Unlike synchronizeBuffer(), this operation never imports host text and
     * therefore never resets the buffer's undo graph. Multiple windows may
     * attach to the same buffer while retaining independent cursors. A
     * successful cross-buffer attachment updates only that window's
     * alternate-buffer identity.
     */
    [[nodiscard]] bool attachBuffer(
        WindowId window,
        BufferId buffer,
        Cursor cursor);
    [[nodiscard]] BufferAttachStatus attachBufferData(
        WindowId window,
        BufferId buffer,
        Cursor cursor);
    [[nodiscard]] bool replaceBufferText(
        BufferId buffer,
        std::u16string text,
        Cursor cursor);
    /**
     * Applies a host-originating UTF-16 edit without echoing BufferEdited or
     * CursorChanged events back to that host.
     *
     * Offsets and lengths are UTF-16 code units, matching Qt text positions.
     * The supplied view cursor is authoritative after the edit and may point
     * one past the final character while Insert mode is active.
     */
    [[nodiscard]] bool applyExternalEdit(
        ViewId view,
        std::size_t offset,
        std::size_t removed,
        std::u16string inserted,
        Cursor cursor);
    /**
     * Offset-based variant for projected editors whose canonical selection
     * is already expressed as one UTF-16 buffer boundary.
     *
     * The cursor offset is resolved after the edit against Core's patched
     * line index, avoiding a host-side whole-document line scan.
     */
    [[nodiscard]] bool applyExternalEditAtOffset(
        ViewId view,
        std::size_t offset,
        std::size_t removed,
        std::u16string inserted,
        std::size_t cursorOffset);
    /**
     * Selection-preserving host edit for native non-modal text surfaces.
     *
     * Both selection boundaries are canonical UTF-16 offsets in the
     * post-edit buffer. Core stores them in the same undo node as the text
     * delta, so undo/redo never depends on a parallel widget history.
     */
    [[nodiscard]] bool applyExternalEditWithSelectionAtOffsets(
        ViewId view,
        std::size_t offset,
        std::size_t removed,
        std::u16string inserted,
        std::size_t selectionAnchorOffset,
        std::size_t cursorOffset);
    /**
     * Atomically applies sequential UTF-16 replacements and selection state.
     *
     * Every range addresses the snapshot produced by the preceding range.
     * The external session validates the complete candidate and publishes
     * one revision/history node, or leaves text, selection, and history
     * unchanged.
     */
    [[nodiscard]] bool applyExternalEditBatchWithSelectionAtOffsets(
        ViewId view, std::vector<vkui::buffer::TransactionEdit> edits,
        std::size_t selectionBeforeAnchor, std::size_t selectionBeforeCursor,
        std::size_t selectionAfterAnchor, std::size_t selectionAfterCursor,
        std::optional<std::uint64_t> group = std::nullopt);
    /**
     * Replays buffer history through the same incremental event stream used
     * by Normal-mode u and Ctrl-R.
     *
     * These entry points remain available while modal key handling is
     * disabled, allowing a platform Undo/Redo shortcut to keep VkCore as the
     * sole business history and QTextDocument as a projection.
     */
    [[nodiscard]] DispatchResult undo(
        WindowId window,
        std::size_t count = 1);
    [[nodiscard]] DispatchResult redo(
        WindowId window,
        std::size_t count = 1);
    /**
     * Completes the host-rendered Ex or search command line.
     *
     * The command line is presentation only; parsing, search state, jump-list
     * updates and semantic command dispatch stay in VkCore.
     */
    [[nodiscard]] DispatchResult submitCommandLine(
        WindowId window,
        CommandLineKind kind,
        std::u16string text);

    /**
     * Starts a transient, Core-owned text prompt on a host input target.
     *
     * This is the structured-surface equivalent of Neovim's command-line or
     * input prompt: VK remains the first key consumer and owns Enter/Escape,
     * while the host's native line editor owns text services, selection and
     * IME composition. The host mirrors text changes back with
     * updatePromptInput(); completion is reported as PromptSubmitted or
     * PromptCancelled events from dispatch(). Only one prompt may be active.
     */
    [[nodiscard]] std::optional<PromptSessionId> beginPromptInput(
        WindowId window,
        InputTargetId inputTarget,
        std::u16string initialText = {});
    [[nodiscard]] bool updatePromptInput(
        PromptSessionId session,
        std::u16string text);
    [[nodiscard]] DispatchResult cancelPromptInput(
        PromptSessionId session);
    [[nodiscard]] std::optional<PromptSessionId>
    activePromptInput() const noexcept;
    [[nodiscard]] bool setViewCursor(
        ViewId view,
        Cursor cursor);
    /** Synchronizes a native host selection without entering Visual mode. */
    [[nodiscard]] bool setViewSelectionAtOffsets(
        ViewId view,
        std::size_t anchorOffset,
        std::size_t cursorOffset);
    /** Returns canonical native-host selection boundaries for one view. */
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
    viewSelectionOffsets(ViewId view) const;
    /** Records a non-local motion for native CTRL-O/CTRL-I navigation. */
    [[nodiscard]] bool jumpViewCursor(
        ViewId view,
        Cursor cursor);
    /** Converts one logical cursor to its canonical UTF-16 buffer offset. */
    [[nodiscard]] std::optional<std::size_t> bufferOffset(
        BufferId buffer,
        Cursor cursor) const;
    /** Converts a UTF-16 buffer offset to a clamped logical cursor. */
    [[nodiscard]] std::optional<Cursor> cursorForBufferOffset(
        BufferId buffer,
        std::size_t offset) const;
    [[nodiscard]] bool setBufferReadOnly(
        BufferId buffer,
        bool readOnly);
    /** Buffer-local tab width used by movement, block operators, and UI. */
    [[nodiscard]] bool setBufferTabStop(
        BufferId buffer,
        std::size_t columns);
    /** Introspection for performance telemetry and regression tests. */
    [[nodiscard]] std::optional<DisplayLayoutCacheStats>
    displayLayoutCacheStats(BufferId buffer) const;
    /**
     * Resolves a UTF-16 buffer cursor to Core's logical-line display cell.
     *
     * A renderer uses QTextLine::xToCursor() only to measure the destination
     * cursor, then calls this query instead of duplicating tab, grapheme,
     * emoji, or East-Asian-width rules. Invalid grapheme-interior positions
     * return nullopt. After the line's lazy layout is cached, lookup is
     * O(log S), where S is that line's sparse special-cell count.
     */
    [[nodiscard]] std::optional<DisplayPosition> displayPosition(
        BufferId buffer,
        Cursor cursor) const;
    /**
     * Resolves an absolute logical-line display cell back to a UTF-16 cursor.
     *
     * Character mode returns a canonical grapheme start and clamps past-EOL
     * cells to the last character. VisualBlock preserves cells inside tabs;
     * it preserves past-EOL cells only while virtualEditMode() is Block.
     * Lookup is O(log S) after the line's lazy sparse layout is cached.
     */
    [[nodiscard]] std::optional<DisplayPosition>
    displayPositionForColumn(
        BufferId buffer,
        std::size_t line,
        std::size_t absoluteDisplayColumn,
        DisplayColumnMode mode =
            DisplayColumnMode::Character) const;
    [[nodiscard]] std::optional<BufferId> bufferForPath(
        std::string_view path) const;
    /**
     * Returns whether a live view is bound to a buffer at or below path.
     *
     * Hosts use this as the prepare phase of a filesystem rename/move. A
     * detached buffer may represent stale external state and can be replaced
     * after the filesystem transaction; a view-bound buffer must instead be
     * resolved before mutating disk state.
     */
    [[nodiscard]] bool hasAttachedBufferAtOrBelowPath(
        std::string_view path) const;
    [[nodiscard]] bool rebindBufferPath(
        BufferId buffer,
        std::string newPath,
        bool replaceConflictingBuffer = false);
    /**
     * Atomically rebinds loaded buffers at or below an absolute path.
     *
     * Buffer identities, view cursors, and buffer-order positions remain
     * stable. The mutation is rejected without partial changes if any target
     * path is already owned by a buffer outside the moved subtree, unless the
     * host explicitly confirms that its filesystem transaction superseded
     * detached stale target buffers. A buffer still attached to any view is
     * never displaced.
     */
    [[nodiscard]] bool rebindBuffersUnderPath(
        std::string_view oldRoot,
        std::string_view newRoot,
        std::size_t *rebound = nullptr,
        bool replaceConflictingBuffers = false);
    [[nodiscard]] bool removeBuffer(BufferId buffer);
    [[nodiscard]] std::size_t removeBuffersUnderPath(
        std::string_view root);
    [[nodiscard]] bool detachViewBuffer(ViewId view);
    [[nodiscard]] std::optional<BufferSnapshot> buffer(
        BufferId buffer) const;
    /** Returns Core-owned undo/redo and clean-point state without replaying it. */
    [[nodiscard]] std::optional<BufferHistorySnapshot> bufferHistory(
        BufferId buffer) const;
    /** Resets one buffer to a clean root history node without changing text. */
    [[nodiscard]] bool resetBufferHistory(BufferId buffer);
    /** Marks the current Core history node as clean, or explicitly dirty. */
    [[nodiscard]] bool setBufferModified(BufferId buffer, bool modified);
    /**
     * Zero-content metadata for the authoritative VKBuffer data plane.
     *
     * Prefer this plus readBufferRangeAtRevision() in asynchronous renderers;
     * buffer() remains the whole-text compatibility snapshot for existing
     * editor hosts.
     */
    [[nodiscard]] std::optional<vkui::buffer::Descriptor>
    bufferDataDescriptor(BufferId buffer) const;
    /** Canonical UTF-16 units owned by VkCore (zero for external sessions). */
    [[nodiscard]] std::optional<std::size_t>
    bufferResidentCodeUnits(BufferId buffer) const noexcept;
    /** Bounded scalar-read cache currently populated for an external session. */
    [[nodiscard]] std::optional<std::size_t> bufferCachedCodeUnits(BufferId buffer) const noexcept;
    [[nodiscard]] std::optional<BufferAuthoritySnapshot>
    bufferAuthority(BufferId buffer) const noexcept;
    /** Preserves Pending/Stale/Unavailable provider states for async hosts. */
    [[nodiscard]] vkui::buffer::RangeRead readBufferDataRange(
        BufferId buffer,
        vkui::buffer::RangeRequest request) const noexcept;
    [[nodiscard]] bool requestBufferPrefetch(
        BufferId buffer,
        vkui::buffer::PrefetchRequest request,
        std::stop_token stopToken,
        vkui::buffer::PrefetchCompletion completion) const noexcept;
    /**
     * Reads at most maxLength UTF-16 code units from a stable buffer revision.
     *
     * This is the generic renderer/plugin data plane. It is deliberately
     * buffer-oriented and has no knowledge of Reader, pagination, chapters,
     * or QWidget lifetimes. Callers can page with the returned offset/size
     * and reject stale asynchronous work by comparing revision.
     */
    [[nodiscard]] std::optional<BufferRangeSnapshot>
    readBufferRange(
        BufferId buffer,
        std::size_t offset,
        std::size_t maxLength) const;
    /** Reads only if the requested authoritative revision is still current. */
    [[nodiscard]] std::optional<BufferRangeSnapshot>
    readBufferRangeAtRevision(
        BufferId buffer,
        std::size_t offset,
        std::size_t maxLength,
        vkui::buffer::Revision expectedRevision) const;
    [[nodiscard]] std::optional<std::string> bufferPath(
        BufferId buffer) const;
    [[nodiscard]] std::optional<BufferSnapshot> activeBuffer(
        ViewId view) const;
    [[nodiscard]] std::size_t bufferCount() const noexcept;
    void clearWorkspace();

    /**
     * Submits the original Qt event at the application input boundary.
     *
     * The core performs one cached canonicalization into its native typeahead
     * representation and shares it across ownership probes and dispatch.
     * There is no mirrored Qt key enum and no runtime string notation
     * conversion.
     */
    [[nodiscard]] DispatchResult dispatch(
        ViewId view,
        InputTargetId inputTarget,
        const QKeyEvent &event);
    /**
     * True when the physical event carries a VK input atom.
     *
     * Standalone modifier transitions have no terminal/Vim key identity.
     * Hosts must let them update Qt's modifier state without treating them as
     * command boundaries or cancelling pending typeahead.
     */
    [[nodiscard]] bool hasCanonicalInput(
        const QKeyEvent &event) const;
    [[nodiscard]] bool shouldCapture(
        ViewId view,
        const QKeyEvent &event) const;
    /**
     * Returns whether an already-waiting mapping consumes this physical key.
     *
     * Hosts use this before shortcut arbitration. A key that cannot extend
     * the pending mapping first resolves the real typeahead; ownership is
     * then decided again in the resulting mode.
     */
    [[nodiscard]] bool pendingMappingConsumes(
        ViewId view,
        const QKeyEvent &event) const;

    /**
     * Applies a host input boundary before focus, pointer, buffer, or window
     * state changes. Insert prefixes are resolved on their original target;
     * incomplete Normal commands and operators are cancelled.
     *
     * dispatch() applies the same transition when callers change contexts,
     * so a host cannot accidentally retarget queued input.
     */
    [[nodiscard]] DispatchResult transitionInputContext(
        ViewId view,
        InputTargetId inputTarget,
        bool force = false);

    /**
     * Resolves an incomplete mapping after timeoutlen. Stale timer
     * generations are ignored, so the Qt host never has to replay events.
     */
    [[nodiscard]] DispatchResult mappingTimedOut(
        std::uint64_t generation);
    [[nodiscard]] DispatchResult flushPendingInput();

    /**
     * Returns a detached snapshot of direct mappings below the currently
     * pending prefix. The mapping resolver remains the only source of truth;
     * presentation layers such as which-key never maintain a second keymap.
     */
    [[nodiscard]] std::optional<MappingPrefixSnapshot>
    pendingMappingSnapshot(
        std::uint64_t generation) const;
    /**
     * Returns the complete Core-owned hint state for the current prefix.
     *
     * This includes both user/plugin mappings and native Normal grammar.
     * pendingMappingSnapshot() remains as a compatibility spelling while
     * hosts migrate to this API.
     */
    [[nodiscard]] std::optional<InputHintSnapshot>
    pendingInputHint(
        std::uint64_t generation) const;

    /**
     * Activates presentation controls for one still-live input hint.
     *
     * The host calls this only after the delayed hint surface is actually
     * visible. Until then Escape, Backspace and Ctrl-D/U retain their normal
     * Core meaning. Paging is emitted through the supplied semantic command
     * ids; the session never owns or copies mapping data.
     */
    [[nodiscard]] bool beginInputHintSession(
        std::uint64_t generation,
        std::string pageDownCommand,
        std::string pageUpCommand);
    /**
     * Ends only the presentation lease; pending real typeahead survives.
     * A detached mapping-root view created by Backspace has no typeahead and
     * is discarded with the lease.
     */
    void endInputHintSession();
    [[nodiscard]] bool inputHintSessionActive() const noexcept;

    /**
     * Commits a renderer-measured wrapped-row motion.
     *
     * The request id comes from DisplayMotionRequested. A missing resolution
     * rejects the transaction. Stale ids, buffer revisions, and invalid UTF-16
     * positions are rejected without moving or editing the authoritative
     * buffer. Completion also resumes mapped typeahead behind the same host
     * barrier, so a mapping containing gj cannot overtake its measured move.
     */
    [[nodiscard]] DispatchResult resolveDisplayMotion(
        DisplayMotionRequestId request,
        std::optional<DisplayMotionResolution> resolution);

    /**
     * Completes a host action transaction and, on success, resumes any
     * remaining typeahead in the host's resulting input context.
     *
     * Stale or duplicate generations are ignored. A rejected transaction
     * discards its remaining mapping RHS; an accepted transaction may return
     * another barrier when the resumed RHS requests a second host action.
     */
    [[nodiscard]] DispatchResult acknowledgeHostBarrier(
        std::uint64_t generation,
        bool committed,
        ViewId resultingView,
        InputTargetId resultingTarget);

    void setLeader(std::u32string notation);
    void setLocalLeader(std::u32string notation);
    /**
     * Validates every mapping scope against the current live Core identities,
     * then publishes the complete batch with one resolver rebuild.
     *
     * Failure is atomic and does not consume mapping ids.
     */
    [[nodiscard]] std::vector<MappingId> addMappings(
        std::span<const MappingDefinition> definitions,
        std::string *error = nullptr);
    [[nodiscard]] MappingId addMapping(
        const MappingDefinition &definition,
        std::string *error = nullptr);
    [[nodiscard]] bool removeMapping(MappingId id);
    /**
     * Revokes a mapping-owner batch with one resolver rebuild.
     *
     * If the batch participates in the currently waiting prefix, that exact
     * input transaction is cancelled and its old generation is returned so a
     * host can close delayed hint UI. Unrelated pending prefixes survive.
     */
    [[nodiscard]] MappingRevocation removeMappings(
        std::span<const MappingId> ids);
    void setMappingTimeoutMilliseconds(int value);
    [[nodiscard]] int mappingTimeoutMilliseconds() const noexcept;

    /** Owner-scoped Ex aliases for trusted core-plugin activation. */
    [[nodiscard]] VkUserCommandRegistry &userCommands() noexcept;
    [[nodiscard]] const VkUserCommandRegistry &
    userCommands() const noexcept;

    void cancelPendingInput();

private:
    class Implementation;
    std::unique_ptr<Implementation> m_impl;
};

} // namespace vkui::vk
