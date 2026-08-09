#include <vkui/buffer/BufferStorage.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace vkui::buffer {
namespace {

[[nodiscard]] RangeRead statusRead(
    const ReadStatus status,
    const Descriptor &descriptor,
    const std::size_t offset)
{
    return RangeRead{
        status,
        descriptor.identity,
        descriptor.revision,
        offset,
        descriptor.size,
        {}};
}

[[nodiscard]] bool validDescriptor(
    const Descriptor &descriptor) noexcept
{
    return !descriptor.identity.value.empty()
        && descriptor.revision != 0;
}

} // namespace

bool IRangeProvider::requestPrefetch(
    const PrefetchRequest &request,
    const std::stop_token stopToken,
    PrefetchCompletion completion) const
{
    static_cast<void>(request);
    static_cast<void>(stopToken);
    static_cast<void>(completion);
    return false;
}

OwnedTextStorage::OwnedTextStorage() = default;

OwnedTextStorage::OwnedTextStorage(
    Identity identity,
    std::u16string text,
    const Revision revision)
    : m_identity(std::move(identity))
    , m_revision(std::max<Revision>(1, revision))
    , m_text(std::move(text))
{
}

Descriptor OwnedTextStorage::describe() const
{
    return Descriptor{
        m_identity,
        m_revision,
        m_text.size(),
        true};
}

RangeRead OwnedTextStorage::read(
    const RangeRequest &request) const
{
    const Descriptor descriptor = describe();
    if (request.expectedRevision
        && *request.expectedRevision != m_revision) {
        return statusRead(
            ReadStatus::StaleRevision,
            descriptor,
            request.offset);
    }
    if (request.offset > m_text.size()) {
        return statusRead(
            ReadStatus::OutOfBounds,
            descriptor,
            request.offset);
    }
    const std::size_t length = std::min(
        request.maximumLength,
        m_text.size() - request.offset);
    return RangeRead{
        ReadStatus::Ok,
        m_identity,
        m_revision,
        request.offset,
        m_text.size(),
        m_text.substr(request.offset, length)};
}

EditResult OwnedTextStorage::edit(EditRequest request)
{
    return edit(
        request.offset,
        request.removedLength,
        request.inserted,
        request.expectedRevision);
}

EditResult OwnedTextStorage::edit(
    const std::size_t offset,
    const std::size_t removedLength,
    const std::u16string_view inserted,
    const std::optional<Revision> expectedRevision)
{
    if (expectedRevision
        && *expectedRevision != m_revision) {
        return EditResult{
            EditStatus::StaleRevision,
            m_revision,
            m_text.size()};
    }
    if (offset > m_text.size()
        || removedLength > m_text.size() - offset) {
        return EditResult{
            EditStatus::OutOfBounds,
            m_revision,
            m_text.size()};
    }
    const auto first = m_text.cbegin()
        + static_cast<std::ptrdiff_t>(offset);
    if (removedLength == inserted.size()
        && std::equal(
            inserted.cbegin(),
            inserted.cend(),
            first)) {
        return EditResult{
            EditStatus::Unchanged,
            m_revision,
            m_text.size()};
    }
    m_text.replace(
        offset,
        removedLength,
        inserted);
    return EditResult{
        EditStatus::Applied,
        nextRevision(),
        m_text.size()};
}

EditResult OwnedTextStorage::replace(
    std::u16string text,
    const std::optional<Revision> expectedRevision)
{
    if (expectedRevision && *expectedRevision != m_revision) {
        return EditResult{
            EditStatus::StaleRevision,
            m_revision,
            m_text.size()};
    }
    if (text == m_text) {
        return EditResult{
            EditStatus::Unchanged,
            m_revision,
            m_text.size()};
    }
    m_text = std::move(text);
    return EditResult{
        EditStatus::Applied,
        nextRevision(),
        m_text.size()};
}

Revision OwnedTextStorage::nextRevision() noexcept
{
    if (m_revision == std::numeric_limits<Revision>::max()) {
        // Revision zero is reserved for invalid descriptors. Wrapping to one
        // remains unambiguous for practical cache lifetimes.
        m_revision = 1;
    } else {
        ++m_revision;
    }
    return m_revision;
}

BufferStorage::BufferStorage() = default;

BufferStorage::BufferStorage(
    Backing backing,
    const std::size_t maximumReadLength)
    : m_backing(std::move(backing))
    , m_maximumReadLength(maximumReadLength)
{
}

BufferStorage BufferStorage::owned(
    Identity identity,
    std::u16string text,
    const Revision revision,
    const std::size_t maximumReadLength)
{
    if (identity.value.empty()) {
        return {};
    }
    return BufferStorage{
        OwnedTextStorage{
            std::move(identity),
            std::move(text),
            revision},
        maximumReadLength};
}

BufferStorage BufferStorage::provider(
    std::shared_ptr<const IRangeProvider> provider,
    const std::size_t maximumReadLength)
{
    if (!provider) {
        return {};
    }
    return BufferStorage{
        std::move(provider),
        maximumReadLength};
}

bool BufferStorage::valid() const noexcept
{
    return !std::holds_alternative<Empty>(m_backing);
}

bool BufferStorage::isOwned() const noexcept
{
    return std::holds_alternative<OwnedTextStorage>(m_backing);
}

bool BufferStorage::isProviderBacked() const noexcept
{
    return std::holds_alternative<
        std::shared_ptr<const IRangeProvider>>(m_backing);
}

std::size_t BufferStorage::maximumReadLength() const noexcept
{
    return m_maximumReadLength;
}

std::size_t BufferStorage::residentCodeUnits() const noexcept
{
    const auto *owned = ownedText();
    return owned == nullptr ? 0 : owned->size();
}

std::optional<Descriptor> BufferStorage::describe() const noexcept
{
    try {
        if (const auto *owned = ownedText()) {
            return owned->describe();
        }
        const auto *provider = std::get_if<
            std::shared_ptr<const IRangeProvider>>(
                &m_backing);
        if (provider == nullptr || !*provider) {
            return std::nullopt;
        }
        Descriptor descriptor = (*provider)->describe();
        if (!validDescriptor(descriptor)) {
            return std::nullopt;
        }
        descriptor.editable = false;
        return descriptor;
    } catch (...) {
        return std::nullopt;
    }
}

RangeRead BufferStorage::read(
    RangeRequest request) const noexcept
{
    const auto descriptor = describe();
    if (!descriptor) {
        return {};
    }
    if (request.expectedRevision
        && *request.expectedRevision
            != descriptor->revision) {
        return statusRead(
            ReadStatus::StaleRevision,
            *descriptor,
            request.offset);
    }
    if (request.offset > descriptor->size) {
        return statusRead(
            ReadStatus::OutOfBounds,
            *descriptor,
            request.offset);
    }
    request.maximumLength = std::min(
        request.maximumLength,
        m_maximumReadLength);
    request.maximumLength = std::min(
        request.maximumLength,
        descriptor->size - request.offset);
    request.expectedRevision = descriptor->revision;

    if (const auto *owned = ownedText()) {
        return owned->read(request);
    }
    if (request.maximumLength == 0) {
        return RangeRead{
            ReadStatus::Ok,
            descriptor->identity,
            descriptor->revision,
            request.offset,
            descriptor->size,
            {}};
    }

    const auto *provider = std::get_if<
        std::shared_ptr<const IRangeProvider>>(
            &m_backing);
    if (provider == nullptr || !*provider) {
        return statusRead(
            ReadStatus::Unavailable,
            *descriptor,
            request.offset);
    }
    try {
        RangeRead result = (*provider)->read(request);
        if (result.status == ReadStatus::Pending) {
            const bool validPending = result.text.empty()
                && result.identity == descriptor->identity
                && result.revision == descriptor->revision
                && result.offset == request.offset
                && result.totalSize == descriptor->size;
            return validPending
                ? result
                : statusRead(
                      ReadStatus::ContractViolation,
                      *descriptor,
                      request.offset);
        }
        if (result.status == ReadStatus::StaleRevision) {
            return result;
        }
        if (result.status == ReadStatus::Unavailable) {
            return statusRead(
                ReadStatus::Unavailable,
                *descriptor,
                request.offset);
        }
        const bool valid = result.status == ReadStatus::Ok
            && result.identity == descriptor->identity
            && result.revision == descriptor->revision
            && result.offset == request.offset
            && result.totalSize == descriptor->size
            && result.text.size() <= request.maximumLength
            && result.text.size()
                <= descriptor->size - request.offset;
        if (!valid) {
            return statusRead(
                ReadStatus::ContractViolation,
                *descriptor,
                request.offset);
        }
        return result;
    } catch (...) {
        return statusRead(
            ReadStatus::Unavailable,
            *descriptor,
            request.offset);
    }
}

bool BufferStorage::requestPrefetch(
    PrefetchRequest request,
    const std::stop_token stopToken,
    PrefetchCompletion completion) const noexcept
{
    if (!completion || stopToken.stop_requested()) {
        return false;
    }
    const auto descriptor = describe();
    if (!descriptor
        || request.expectedRevision != descriptor->revision
        || request.offset > descriptor->size) {
        return false;
    }
    request.maximumLength = std::min(
        {request.maximumLength,
         m_maximumReadLength,
         descriptor->size - request.offset});
    const auto *provider = std::get_if<
        std::shared_ptr<const IRangeProvider>>(
            &m_backing);
    if (provider == nullptr || !*provider) {
        return false;
    }
    try {
        const PrefetchRequest validatedRequest = request;
        return (*provider)->requestPrefetch(
            request,
            stopToken,
            [validatedRequest,
             completion = std::move(completion)](
                PrefetchResult result) mutable {
                const bool commonValid =
                    result.generation
                        == validatedRequest.generation
                    && result.offset
                        == validatedRequest.offset;
                const bool readyValid =
                    result.status != PrefetchStatus::Ready
                    || (result.revision
                            == validatedRequest
                                   .expectedRevision
                        && result.availableLength
                            <= validatedRequest
                                   .maximumLength);
                if (!commonValid || !readyValid) {
                    result = PrefetchResult{
                        PrefetchStatus::Unavailable,
                        validatedRequest.expectedRevision,
                        validatedRequest.offset,
                        0,
                        validatedRequest.generation};
                }
                completion(std::move(result));
            });
    } catch (...) {
        return false;
    }
}

EditResult BufferStorage::edit(EditRequest request)
{
    auto *owned = ownedText();
    if (owned == nullptr) {
        const auto descriptor = describe();
        return EditResult{
            EditStatus::ReadOnly,
            descriptor ? descriptor->revision : 0,
            descriptor ? descriptor->size : 0};
    }
    return owned->edit(std::move(request));
}

EditResult BufferStorage::editOwned(
    const std::size_t offset,
    const std::size_t removedLength,
    const std::u16string_view inserted,
    const std::optional<Revision> expectedRevision)
{
    auto *owned = ownedText();
    if (owned == nullptr) {
        const auto descriptor = describe();
        return EditResult{
            EditStatus::ReadOnly,
            descriptor ? descriptor->revision : 0,
            descriptor ? descriptor->size : 0};
    }
    return owned->edit(
        offset,
        removedLength,
        inserted,
        expectedRevision);
}

EditResult BufferStorage::replaceOwned(
    std::u16string text,
    const std::optional<Revision> expectedRevision)
{
    auto *owned = ownedText();
    if (owned == nullptr) {
        const auto descriptor = describe();
        return EditResult{
            EditStatus::ReadOnly,
            descriptor ? descriptor->revision : 0,
            descriptor ? descriptor->size : 0};
    }
    return owned->replace(
        std::move(text), expectedRevision);
}

} // namespace vkui::buffer
