#include <vkui/buffer/BufferStorage.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>

namespace {

using namespace vkui::buffer;

[[noreturn]] void fail(
    const char *expression,
    const int line)
{
    std::cerr << "failed at line " << line << ": "
              << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define VKBUFFER_CHECK(expression) \
    do { \
        if (!(expression)) { \
            fail(#expression, __LINE__); \
        } \
    } while (false)

class VirtualLargeProvider final : public IRangeProvider
{
public:
    explicit VirtualLargeProvider(
        const std::size_t size)
        : m_size(size)
    {
    }

    [[nodiscard]] Descriptor describe() const override
    {
        ++describeCalls;
        return Descriptor{
            Identity{"large-source"},
            currentRevision,
            m_size,
            false};
    }

    [[nodiscard]] RangeRead read(
        const RangeRequest &request) const override
    {
        ++readCalls;
        largestRequest = std::max(
            largestRequest, request.maximumLength);
        if (!request.expectedRevision
            || *request.expectedRevision
                != currentRevision) {
            return RangeRead{
                ReadStatus::StaleRevision,
                Identity{"large-source"},
                currentRevision,
                request.offset,
                m_size,
                {}};
        }
        const std::size_t length = std::min(
            request.maximumLength,
            m_size - request.offset);
        return RangeRead{
            ReadStatus::Ok,
            Identity{"large-source"},
            currentRevision,
            request.offset,
            m_size,
            std::u16string(length, u'x')};
    }

    std::size_t m_size = 0;
    mutable std::size_t describeCalls = 0;
    mutable std::size_t readCalls = 0;
    mutable std::size_t largestRequest = 0;
    Revision currentRevision = 7;
};

class NonResidentProvider final : public IRangeProvider
{
public:
    [[nodiscard]] Descriptor describe() const override
    {
        return Descriptor{
            Identity{"nonresident-source"},
            41,
            sourceSize,
            false};
    }

    [[nodiscard]] RangeRead read(
        const RangeRequest &request) const override
    {
        ++readCalls;
        largestSynchronousRequest = std::max(
            largestSynchronousRequest,
            request.maximumLength);
        // A real implementation would check a bounded resident-page cache.
        // It must never perform the decode or I/O from this call.
        return RangeRead{
            ReadStatus::Pending,
            Identity{"nonresident-source"},
            41,
            request.offset,
            sourceSize,
            {}};
    }

    [[nodiscard]] bool requestPrefetch(
        const PrefetchRequest &request,
        const std::stop_token stopToken,
        PrefetchCompletion completion) const override
    {
        ++prefetchCalls;
        largestPrefetchRequest = std::max(
            largestPrefetchRequest,
            request.maximumLength);
        if (stopToken.stop_requested()) {
            return false;
        }
        completion(PrefetchResult{
            PrefetchStatus::Ready,
            request.expectedRevision,
            request.offset,
            request.maximumLength,
            request.generation});
        return true;
    }

    static constexpr std::size_t sourceSize =
        512U * 1024U * 1024U;
    mutable std::size_t readCalls = 0;
    mutable std::size_t prefetchCalls = 0;
    mutable std::size_t largestSynchronousRequest = 0;
    mutable std::size_t largestPrefetchRequest = 0;
};

void ownedTextEditsAreRevisionedAndBounded()
{
    BufferStorage storage = BufferStorage::owned(
        Identity{"owned-1"}, u"alpha\nbeta", 11, 4);
    VKBUFFER_CHECK(storage.valid());
    VKBUFFER_CHECK(storage.isOwned());
    VKBUFFER_CHECK(storage.residentCodeUnits() == 10);

    const auto first = storage.read(
        RangeRequest{0, 100, 11});
    VKBUFFER_CHECK(first.ok());
    VKBUFFER_CHECK(first.text == u"alph");
    VKBUFFER_CHECK(first.totalSize == 10);
    VKBUFFER_CHECK(first.revision == 11);

    auto changed = storage.edit(
        EditRequest{6, 4, u"BETA!", 11});
    VKBUFFER_CHECK(changed.status == EditStatus::Applied);
    VKBUFFER_CHECK(changed.revision == 12);
    VKBUFFER_CHECK(changed.size == 11);

    const auto stale = storage.read(
        RangeRequest{0, 4, 11});
    VKBUFFER_CHECK(
        stale.status == ReadStatus::StaleRevision);
    const auto tail = storage.read(
        RangeRequest{6, 100, 12});
    VKBUFFER_CHECK(tail.ok());
    VKBUFFER_CHECK(tail.text == u"BETA");
    VKBUFFER_CHECK(!tail.atEnd());

    const auto unchanged = storage.replaceOwned(
        u"alpha\nBETA!", 12);
    VKBUFFER_CHECK(
        unchanged.status == EditStatus::Unchanged);
    VKBUFFER_CHECK(unchanged.revision == 12);
    VKBUFFER_CHECK(
        storage.edit(EditRequest{99, 0, {}, 12}).status
        == EditStatus::OutOfBounds);
}

void providerReadsNeverMaterializeTheWholeSource()
{
    constexpr std::size_t sourceSize =
        64U * 1024U * 1024U;
    auto provider = std::make_shared<VirtualLargeProvider>(
        sourceSize);
    BufferStorage storage = BufferStorage::provider(
        provider, 4096);

    VKBUFFER_CHECK(storage.isProviderBacked());
    VKBUFFER_CHECK(storage.residentCodeUnits() == 0);
    const auto range = storage.read(
        RangeRequest{sourceSize / 2, sourceSize, 7});
    VKBUFFER_CHECK(range.ok());
    VKBUFFER_CHECK(range.text.size() == 4096);
    VKBUFFER_CHECK(range.totalSize == sourceSize);
    VKBUFFER_CHECK(provider->largestRequest == 4096);
    VKBUFFER_CHECK(storage.residentCodeUnits() == 0);

    const std::size_t reads = provider->readCalls;
    const auto empty = storage.read(
        RangeRequest{sourceSize, 0, 7});
    VKBUFFER_CHECK(empty.ok());
    VKBUFFER_CHECK(empty.atEnd());
    VKBUFFER_CHECK(provider->readCalls == reads);
}

void providerRevisionAndContractAreStable()
{
    auto provider = std::make_shared<VirtualLargeProvider>(128);
    BufferStorage storage = BufferStorage::provider(
        provider, 16);

    const auto original = storage.read(
        RangeRequest{8, 8, 7});
    VKBUFFER_CHECK(original.ok());
    provider->currentRevision = 8;
    const auto stale = storage.read(
        RangeRequest{8, 8, 7});
    VKBUFFER_CHECK(
        stale.status == ReadStatus::StaleRevision);
    const auto current = storage.read(
        RangeRequest{120, 16, 8});
    VKBUFFER_CHECK(current.ok());
    VKBUFFER_CHECK(current.text.size() == 8);
    VKBUFFER_CHECK(current.atEnd());

    const auto pastEnd = storage.read(
        RangeRequest{129, 1, 8});
    VKBUFFER_CHECK(
        pastEnd.status == ReadStatus::OutOfBounds);
    VKBUFFER_CHECK(
        storage.edit(EditRequest{}).status
        == EditStatus::ReadOnly);
}

void nonResidentDataNeverBlocksOrMaterializesWholeSource()
{
    auto provider = std::make_shared<NonResidentProvider>();
    BufferStorage storage = BufferStorage::provider(
        provider, 8192);
    const auto pending = storage.read(
        RangeRequest{100'000, NonResidentProvider::sourceSize, 41});
    VKBUFFER_CHECK(pending.status == ReadStatus::Pending);
    VKBUFFER_CHECK(pending.text.empty());
    VKBUFFER_CHECK(provider->largestSynchronousRequest == 8192);
    VKBUFFER_CHECK(storage.residentCodeUnits() == 0);

    std::stop_source stop;
    bool completed = false;
    const bool scheduled = storage.requestPrefetch(
        PrefetchRequest{
            100'000,
            NonResidentProvider::sourceSize,
            41,
            99},
        stop.get_token(),
        [&completed](const PrefetchResult result) {
            completed = result.status == PrefetchStatus::Ready
                && result.generation == 99
                && result.availableLength == 8192;
        });
    VKBUFFER_CHECK(scheduled);
    VKBUFFER_CHECK(completed);
    VKBUFFER_CHECK(provider->largestPrefetchRequest == 8192);
    VKBUFFER_CHECK(storage.residentCodeUnits() == 0);
}

} // namespace

int main()
{
    ownedTextEditsAreRevisionedAndBounded();
    providerReadsNeverMaterializeTheWholeSource();
    providerRevisionAndContractAreStable();
    nonResidentDataNeverBlocksOrMaterializesWholeSource();
    return EXIT_SUCCESS;
}
