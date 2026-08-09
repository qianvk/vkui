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

enum class EditStatus : std::uint8_t
{
    Applied,
    Unchanged,
    StaleRevision,
    OutOfBounds,
    ReadOnly,
};

struct EditRequest final
{
    std::size_t offset = 0;
    std::size_t removedLength = 0;
    std::u16string inserted;
    std::optional<Revision> expectedRevision;
};

struct EditResult final
{
    EditStatus status = EditStatus::ReadOnly;
    Revision revision = 0;
    std::size_t size = 0;

    [[nodiscard]] bool accepted() const noexcept
    {
        return status == EditStatus::Applied
            || status == EditStatus::Unchanged;
    }
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

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool isOwned() const noexcept;
    [[nodiscard]] bool isProviderBacked() const noexcept;
    [[nodiscard]] std::size_t maximumReadLength() const noexcept;
    [[nodiscard]] std::size_t residentCodeUnits() const noexcept;

    [[nodiscard]] std::optional<Descriptor> describe() const noexcept;
    [[nodiscard]] RangeRead read(
        RangeRequest request) const noexcept;
    [[nodiscard]] bool requestPrefetch(
        PrefetchRequest request,
        std::stop_token stopToken,
        PrefetchCompletion completion) const noexcept;
    [[nodiscard]] EditResult edit(EditRequest request);
    [[nodiscard]] EditResult editOwned(
        std::size_t offset,
        std::size_t removedLength,
        std::u16string_view inserted,
        std::optional<Revision> expectedRevision = std::nullopt);
    [[nodiscard]] EditResult replaceOwned(
        std::u16string text,
        std::optional<Revision> expectedRevision = std::nullopt);

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

    using Backing = std::variant<
        Empty,
        OwnedTextStorage,
        std::shared_ptr<const IRangeProvider>>;

    explicit BufferStorage(
        Backing backing,
        std::size_t maximumReadLength);

    Backing m_backing;
    std::size_t m_maximumReadLength =
        defaultMaximumReadLength;
};

} // namespace vkui::buffer
