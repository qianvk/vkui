#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vkui::buffer {

using Revision = std::uint64_t;

/**
 * Stable logical identity of a data source.
 *
 * The identity is deliberately independent of a path. A host may therefore
 * rename a file without invalidating buffer-local cursors, marks, or layout
 * caches. The concrete UUID/native-file-id policy belongs to the host.
 */
struct Identity final
{
    std::string value;

    friend bool operator==(const Identity &, const Identity &) = default;
};

struct Descriptor final
{
    Identity identity;
    Revision revision = 0;
    std::size_t size = 0;
    bool editable = false;
    /** O(1) host assertion required before VkCore enables modal mutation. */
    bool modalEditingLfOnly = false;
};

enum class ReadStatus : std::uint8_t
{
    Ok,
    Pending,
    StaleRevision,
    OutOfBounds,
    Unavailable,
    ContractViolation,
};

enum class PrefetchStatus : std::uint8_t
{
    Ready,
    Cancelled,
    StaleRevision,
    Unavailable,
};

struct PrefetchRequest final
{
    std::size_t offset = 0;
    std::size_t maximumLength = 0;
    Revision expectedRevision = 0;
    std::uint64_t generation = 0;
};

struct PrefetchResult final
{
    PrefetchStatus status = PrefetchStatus::Unavailable;
    Revision revision = 0;
    std::size_t offset = 0;
    std::size_t availableLength = 0;
    std::uint64_t generation = 0;
};

using PrefetchCompletion =
    std::function<void(PrefetchResult)>;

struct RangeRequest final
{
    std::size_t offset = 0;
    std::size_t maximumLength = 0;
    std::optional<Revision> expectedRevision;
};

/** Detached, bounded data from exactly one source revision. */
struct RangeRead final
{
    ReadStatus status = ReadStatus::Unavailable;
    Identity identity;
    Revision revision = 0;
    std::size_t offset = 0;
    std::size_t totalSize = 0;
    std::u16string text;

    [[nodiscard]] bool ok() const noexcept
    {
        return status == ReadStatus::Ok;
    }

    [[nodiscard]] bool atEnd() const noexcept
    {
        return ok() && offset + text.size() >= totalSize;
    }
};

/**
 * Thread-safe, revisioned random-access source for large/read-only buffers.
 *
 * describe() and read() are strict non-blocking, resident-only operations:
 * they must not touch the filesystem, decompress, parse, or wait on another
 * thread. A nonresident range returns Pending. This invariant keeps modal
 * key dispatch independent of EPUB/PDF I/O latency.
 *
 * Implementations must never return more than RangeRequest::maximumLength
 * UTF-16 code units. A source change returns StaleRevision instead of mixing
 * two revisions. Optional asynchronous preparation is exposed separately by
 * requestPrefetch(); its generation and stop token let the Reader/host
 * discard obsolete viewport work. Exceptions are contained by BufferStorage
 * and become Unavailable results.
 */
class IRangeProvider
{
public:
    virtual ~IRangeProvider() = default;

    [[nodiscard]] virtual Descriptor describe() const = 0;
    [[nodiscard]] virtual RangeRead read(
        const RangeRequest &request) const = 0;

    [[nodiscard]] virtual bool requestPrefetch(
        const PrefetchRequest &request,
        std::stop_token stopToken,
        PrefetchCompletion completion) const;
};

/**
 * Immutable, revision-pinned UTF-16 view of an editable document.
 *
 * A snapshot may share persistent tree/rope nodes with its document. VkCore
 * never asks it for a whole-document pointer: scalar, bounded-range, and
 * logical-line queries are the complete text access contract.
 */
class ITextSnapshot {
  public:
    virtual ~ITextSnapshot() = default;

    [[nodiscard]] virtual Descriptor describe() const = 0;
    [[nodiscard]] virtual RangeRead read(const RangeRequest& request) const = 0;
    [[nodiscard]] virtual std::optional<char16_t> codeUnitAt(std::size_t offset) const = 0;
    [[nodiscard]] virtual std::size_t lineCount() const = 0;
    [[nodiscard]] virtual std::optional<std::size_t> lineStart(std::size_t line) const = 0;
    [[nodiscard]] virtual std::optional<std::size_t> lineForOffset(std::size_t offset) const = 0;
};

enum class EditStatus : std::uint8_t {
    Applied,
    Unchanged,
    /** The host committed, but did not return a usable immutable snapshot. */
    CommittedSnapshotUnavailable,
    /** No host call was made because the external projection is unavailable. */
    Unavailable,
    StaleRevision,
    OutOfBounds,
    ReadOnly,
};

/** Direction-preserving UTF-16 selection boundaries. */
struct TextSelection final {
    std::size_t anchor = 0;
    std::size_t cursor = 0;

    friend bool operator==(const TextSelection&, const TextSelection&) = default;
};

struct EditRequest final
{
    std::size_t offset = 0;
    std::size_t removedLength = 0;
    std::u16string inserted;
    std::optional<Revision> expectedRevision;
    std::optional<TextSelection> selectionBefore;
    std::optional<TextSelection> selectionAfter;
    std::optional<std::uint64_t> group;
};

struct EditResult final
{
    EditStatus status = EditStatus::ReadOnly;
    Revision revision = 0;
    std::size_t size = 0;
    /** Required for an accepted external-session result; unused by owned storage. */
    std::shared_ptr<const ITextSnapshot> committedSnapshot;

    [[nodiscard]] bool accepted() const noexcept
    {
        return status == EditStatus::Applied
            || status == EditStatus::Unchanged;
    }

    [[nodiscard]] bool committed() const noexcept {
        return accepted() || status == EditStatus::CommittedSnapshotUnavailable;
    }
};

struct TransactionEdit final {
    std::size_t offset = 0;
    std::size_t removedLength = 0;
    std::u16string inserted;
};

/** One all-or-nothing text/history/selection commit. */
struct EditTransactionRequest final {
    std::vector<TransactionEdit> edits;
    std::optional<Revision> expectedRevision;
    std::optional<TextSelection> selectionBefore;
    std::optional<TextSelection> selectionAfter;
    std::optional<std::uint64_t> group;
};

struct CommittedEdit final {
    std::size_t offset = 0;
    std::size_t removedLength = 0;
    std::u16string inserted;
};

struct SessionHistory final {
    std::uint64_t current = 0;
    std::optional<std::uint64_t> clean;
    bool canUndo = false;
    bool canRedo = false;
};

enum class HistoryReplayStatus : std::uint8_t {
    Applied,
    Unchanged,
    /** The host committed, but did not return a usable immutable snapshot. */
    CommittedSnapshotUnavailable,
    StaleRevision,
    ReadOnly,
    Unavailable,
};

struct HistoryReplayResult final {
    HistoryReplayStatus status = HistoryReplayStatus::Unavailable;
    Revision revision = 0;
    std::size_t size = 0;
    std::vector<CommittedEdit> edits;
    std::optional<std::size_t> selectionAnchor;
    std::optional<std::size_t> cursor;
    /** Required for every accepted history result, including Unchanged. */
    std::shared_ptr<const ITextSnapshot> committedSnapshot;

    [[nodiscard]] bool accepted() const noexcept {
        return status == HistoryReplayStatus::Applied || status == HistoryReplayStatus::Unchanged;
    }

    [[nodiscard]] bool committed() const noexcept {
        return accepted() || status == HistoryReplayStatus::CommittedSnapshotUnavailable;
    }
};

/**
 * Host-owned mutable document and history boundary.
 *
 * VkCore delegates every external edit/history operation exactly once. Every
 * accepted result carries the immutable post-commit snapshot from that same
 * operation; VkCore never performs a fallible second snapshot() lookup after
 * authority has changed. Implementations must commit atomically: rejected
 * stale/read-only operations leave both text and history unchanged, and an
 * operation must not throw after it has committed.
 */
class IEditableTextSession {
  public:
    virtual ~IEditableTextSession() = default;

    [[nodiscard]] virtual std::shared_ptr<const ITextSnapshot> snapshot() const = 0;
    [[nodiscard]] virtual EditResult applyTransaction(EditTransactionRequest request) = 0;
    [[nodiscard]] virtual SessionHistory history() const = 0;
    [[nodiscard]] virtual std::optional<TextSelection> selection() const = 0;
    [[nodiscard]] virtual bool setSelection(TextSelection selection) = 0;
    [[nodiscard]] virtual HistoryReplayResult undo(std::size_t count) = 0;
    [[nodiscard]] virtual HistoryReplayResult redo(std::size_t count) = 0;
    [[nodiscard]] virtual bool resetHistory() = 0;
    [[nodiscard]] virtual bool setModified(bool modified) = 0;
};

/**
 * Contiguous mutable storage for ordinary editable documents.
 *
 * This type is intentionally thread-confined: VkCore owns it on its command
 * thread, avoiding locks and a second full-document projection. Detached
 * range reads remain explicit copies with caller-controlled bounds.
 */
class OwnedTextStorage final
{
public:
    OwnedTextStorage();
    OwnedTextStorage(
        Identity identity,
        std::u16string text,
        Revision revision = 1);

    [[nodiscard]] const Identity &identity() const noexcept
    {
        return m_identity;
    }

    [[nodiscard]] Revision revision() const noexcept
    {
        return m_revision;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_text.size();
    }

    [[nodiscard]] const std::u16string &text() const noexcept
    {
        return m_text;
    }

    [[nodiscard]] Descriptor describe() const;
    [[nodiscard]] RangeRead read(
        const RangeRequest &request) const;
    [[nodiscard]] EditResult edit(EditRequest request);
    [[nodiscard]] EditResult edit(
        std::size_t offset,
        std::size_t removedLength,
        std::u16string_view inserted,
        std::optional<Revision> expectedRevision = std::nullopt);
    [[nodiscard]] EditResult replace(
        std::u16string text,
        std::optional<Revision> expectedRevision = std::nullopt);

private:
    [[nodiscard]] Revision nextRevision() noexcept;

    Identity m_identity;
    Revision m_revision = 1;
    std::u16string m_text;
};

/**
 * One Core-facing handle over either owned text or a paged provider.
 *
 * The provider alternative stores only a shared interface pointer. It never
 * materializes, mirrors, or caches the whole source. maximumReadLength is a
 * defensive upper bound applied even when a caller requests more data.
 */
class BufferStorage final
{
public:
    static constexpr std::size_t defaultMaximumReadLength =
        1024U * 1024U;

    BufferStorage();

    [[nodiscard]] static BufferStorage owned(
        Identity identity,
        std::u16string text = {},
        Revision revision = 1,
        std::size_t maximumReadLength =
            defaultMaximumReadLength);
    [[nodiscard]] static BufferStorage provider(
        std::shared_ptr<const IRangeProvider> provider,
        std::size_t maximumReadLength =
            defaultMaximumReadLength);

    [[nodiscard]] static BufferStorage
    externalSession(std::shared_ptr<IEditableTextSession> session,
                    std::size_t maximumReadLength = defaultMaximumReadLength,
                    std::size_t scalarCacheCapacity = 4096);

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool isOwned() const noexcept;
    [[nodiscard]] bool isProviderBacked() const noexcept;
    [[nodiscard]] bool isExternalSession() const noexcept;
    [[nodiscard]] bool externalReadFaulted() const noexcept;
    [[nodiscard]] std::optional<Descriptor> lastExternalDescriptor() const noexcept;
    [[nodiscard]] std::size_t maximumReadLength() const noexcept;
    [[nodiscard]] std::size_t residentCodeUnits() const noexcept;
    [[nodiscard]] std::size_t cachedCodeUnits() const noexcept;

    [[nodiscard]] std::optional<Descriptor> describe() const noexcept;
    [[nodiscard]] RangeRead read(
        RangeRequest request) const noexcept;
    [[nodiscard]] bool requestPrefetch(
        PrefetchRequest request,
        std::stop_token stopToken,
        PrefetchCompletion completion) const noexcept;
    [[nodiscard]] EditResult edit(EditRequest request);
    [[nodiscard]] EditResult editTransaction(EditTransactionRequest request);
    [[nodiscard]] EditResult editOwned(
        std::size_t offset,
        std::size_t removedLength,
        std::u16string_view inserted,
        std::optional<Revision> expectedRevision = std::nullopt);
    [[nodiscard]] EditResult replaceOwned(
        std::u16string text,
        std::optional<Revision> expectedRevision = std::nullopt);
    [[nodiscard]] std::optional<char16_t> codeUnitAt(std::size_t offset) const noexcept;
    [[nodiscard]] std::optional<std::u16string> readExact(std::size_t offset,
                                                          std::size_t length) const noexcept;
    [[nodiscard]] std::optional<std::size_t> lineCount() const noexcept;
    [[nodiscard]] std::optional<std::size_t> lineStart(std::size_t line) const noexcept;
    [[nodiscard]] std::optional<std::size_t> lineForOffset(std::size_t offset) const noexcept;
    [[nodiscard]] std::optional<SessionHistory> externalHistory() const noexcept;
    [[nodiscard]] std::optional<TextSelection> externalSelection() const noexcept;
    [[nodiscard]] bool setExternalSelection(TextSelection selection);
    [[nodiscard]] HistoryReplayResult replayExternalHistory(std::size_t count, bool redo);
    [[nodiscard]] bool resetExternalHistory();
    [[nodiscard]] bool setExternalModified(bool modified);
    void invalidateExternalProjection() noexcept;

    /**
     * Direct access for the single-threaded editor engine.
     *
     * Returning nullptr for provider-backed storage makes accidental whole
     * source materialization explicit at the call site.
     */
    [[nodiscard]] const OwnedTextStorage *ownedText() const noexcept
    {
        return std::get_if<OwnedTextStorage>(&m_backing);
    }

    [[nodiscard]] OwnedTextStorage *ownedText() noexcept
    {
        return std::get_if<OwnedTextStorage>(&m_backing);
    }

private:
    struct Empty final
    {
    };

    using Backing = std::variant<Empty, OwnedTextStorage, std::shared_ptr<const IRangeProvider>,
                                 std::shared_ptr<IEditableTextSession>>;

    explicit BufferStorage(
        Backing backing,
        std::size_t maximumReadLength);

    [[nodiscard]] bool adoptExternalSnapshot(std::shared_ptr<const ITextSnapshot> snapshot,
                                             Revision expectedRevision,
                                             std::size_t expectedSize) noexcept;
    void invalidateExternalSnapshot() noexcept;
    void markExternalReadFault() const noexcept;

    Backing m_backing;
    std::shared_ptr<const ITextSnapshot> m_snapshot;
    std::optional<Descriptor> m_lastExternalDescriptor;
    mutable bool m_externalReadFault = false;
    std::size_t m_maximumReadLength =
        defaultMaximumReadLength;
    std::size_t m_scalarCacheCapacity = 0;
    mutable Revision m_scalarCacheRevision = 0;
    mutable std::size_t m_scalarCacheOffset = 0;
    mutable std::u16string m_scalarCache;
};

} // namespace vkui::buffer
