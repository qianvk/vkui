

#include <algorithm>
#include <limits>
#include <utility>
#include <vkui/buffer/BufferStorage.h>

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
    return Descriptor{m_identity, m_revision, m_text.size(), true, true};
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
        return EditResult{EditStatus::StaleRevision, m_revision, m_text.size(), {}};
    }
    if (offset > m_text.size()
        || removedLength > m_text.size() - offset) {
        return EditResult{EditStatus::OutOfBounds, m_revision, m_text.size(), {}};
    }
    const auto first = m_text.cbegin()
        + static_cast<std::ptrdiff_t>(offset);
    if (removedLength == inserted.size()
        && std::equal(
            inserted.cbegin(),
            inserted.cend(),
            first)) {
        return EditResult{EditStatus::Unchanged, m_revision, m_text.size(), {}};
    }
    m_text.replace(
        offset,
        removedLength,
        inserted);
    return EditResult{EditStatus::Applied, nextRevision(), m_text.size(), {}};
}

EditResult OwnedTextStorage::replace(
    std::u16string text,
    const std::optional<Revision> expectedRevision)
{
    if (expectedRevision && *expectedRevision != m_revision) {
        return EditResult{EditStatus::StaleRevision, m_revision, m_text.size(), {}};
    }
    if (text == m_text) {
        return EditResult{EditStatus::Unchanged, m_revision, m_text.size(), {}};
    }
    m_text = std::move(text);
    return EditResult{EditStatus::Applied, nextRevision(), m_text.size(), {}};
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

BufferStorage BufferStorage::externalSession(std::shared_ptr<IEditableTextSession> session,
                                             const std::size_t maximumReadLength,
                                             const std::size_t scalarCacheCapacity) {
    if (!session || maximumReadLength == 0) {
        return {};
    }
    std::shared_ptr<const ITextSnapshot> snapshot;
    try {
        snapshot = session->snapshot();
    } catch (...) {
        return {};
    }
    if (!snapshot) {
        return {};
    }
    Descriptor descriptor;
    try {
        descriptor = snapshot->describe();
        if (!validDescriptor(descriptor)) {
            return {};
        }
    } catch (...) {
        return {};
    }
    BufferStorage storage{std::move(session), maximumReadLength};
    storage.m_snapshot = std::move(snapshot);
    storage.m_lastExternalDescriptor = std::move(descriptor);
    storage.m_scalarCacheCapacity = std::min(scalarCacheCapacity, maximumReadLength);
    return storage;
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

bool BufferStorage::isExternalSession() const noexcept {
    return std::holds_alternative<std::shared_ptr<IEditableTextSession>>(m_backing);
}

bool BufferStorage::externalReadFaulted() const noexcept {
    return isExternalSession() && m_externalReadFault;
}

std::optional<Descriptor> BufferStorage::lastExternalDescriptor() const noexcept {
    return isExternalSession() ? m_lastExternalDescriptor : std::nullopt;
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

std::size_t BufferStorage::cachedCodeUnits() const noexcept {
    return isExternalSession() ? m_scalarCache.size() : 0;
}

std::optional<Descriptor> BufferStorage::describe() const noexcept
{
    try {
        if (const auto *owned = ownedText()) {
            return owned->describe();
        }
        if (isExternalSession()) {
            if (m_externalReadFault || !m_snapshot) {
                return std::nullopt;
            }
            Descriptor descriptor = m_snapshot->describe();
            if (!validDescriptor(descriptor) || !m_lastExternalDescriptor ||
                descriptor.identity != m_lastExternalDescriptor->identity ||
                descriptor.revision != m_lastExternalDescriptor->revision ||
                descriptor.size != m_lastExternalDescriptor->size) {
                markExternalReadFault();
                return std::nullopt;
            }
            return descriptor;
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
        if (isExternalSession()) {
            markExternalReadFault();
        }
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

    RangeRead result;
    try {
        if (isExternalSession()) {
            if (!m_snapshot) {
                return statusRead(ReadStatus::Unavailable, *descriptor, request.offset);
            }
            result = m_snapshot->read(request);
        } else {
            const auto* provider = std::get_if<std::shared_ptr<const IRangeProvider>>(&m_backing);
            if (provider == nullptr || !*provider) {
                return statusRead(ReadStatus::Unavailable, *descriptor, request.offset);
            }
            result = (*provider)->read(request);
        }
    } catch (...) {
        if (isExternalSession()) {
            markExternalReadFault();
        }
        return statusRead(ReadStatus::Unavailable, *descriptor, request.offset);
    }
    try {
        if (result.status == ReadStatus::Pending) {
            if (isExternalSession()) {
                markExternalReadFault();
                return statusRead(ReadStatus::Unavailable, *descriptor, request.offset);
            }
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
            if (isExternalSession()) {
                markExternalReadFault();
                return statusRead(ReadStatus::Unavailable, *descriptor, request.offset);
            }
            return result;
        }
        if (result.status == ReadStatus::Unavailable) {
            if (isExternalSession()) {
                markExternalReadFault();
            }
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
            if (isExternalSession()) {
                markExternalReadFault();
                return statusRead(ReadStatus::Unavailable, *descriptor, request.offset);
            }
            return statusRead(
                ReadStatus::ContractViolation,
                *descriptor,
                request.offset);
        }
        return result;
    } catch (...) {
        if (isExternalSession()) {
            markExternalReadFault();
        }
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
    if (owned != nullptr) {
        return owned->edit(std::move(request));
    }
    if (isExternalSession() && (m_externalReadFault || !m_snapshot)) {
        return EditResult{EditStatus::Unavailable,
                          m_lastExternalDescriptor ? m_lastExternalDescriptor->revision : 0,
                          m_lastExternalDescriptor ? m_lastExternalDescriptor->size : 0,
                          {}};
    }
    auto* session = std::get_if<std::shared_ptr<IEditableTextSession>>(&m_backing);
    if (session == nullptr || !*session) {
        const auto descriptor = describe();
        return EditResult{EditStatus::ReadOnly,
                          descriptor ? descriptor->revision : 0,
                          descriptor ? descriptor->size : 0,
                          {}};
    }
    try {
        EditTransactionRequest transaction;
        transaction.edits.push_back(
            TransactionEdit{request.offset, request.removedLength, std::move(request.inserted)});
        transaction.expectedRevision = request.expectedRevision;
        transaction.selectionBefore = request.selectionBefore;
        transaction.selectionAfter = request.selectionAfter;
        transaction.group = request.group;
        EditResult result = (*session)->applyTransaction(std::move(transaction));
        if (result.accepted() &&
            !adoptExternalSnapshot(result.committedSnapshot, result.revision, result.size)) {
            result.status = EditStatus::CommittedSnapshotUnavailable;
            result.committedSnapshot.reset();
        }
        return result;
    } catch (...) {
        const auto descriptor = m_lastExternalDescriptor;
        invalidateExternalSnapshot();
        return EditResult{EditStatus::CommittedSnapshotUnavailable,
                          descriptor ? descriptor->revision : 0,
                          descriptor ? descriptor->size : 0,
                          {}};
    }
}

EditResult BufferStorage::editTransaction(EditTransactionRequest request) {
    if (isExternalSession() && (m_externalReadFault || !m_snapshot)) {
        return EditResult{EditStatus::Unavailable,
                          m_lastExternalDescriptor ? m_lastExternalDescriptor->revision : 0,
                          m_lastExternalDescriptor ? m_lastExternalDescriptor->size : 0,
                          {}};
    }
    auto* session = std::get_if<std::shared_ptr<IEditableTextSession>>(&m_backing);
    if (session == nullptr || !*session) {
        const auto descriptor = describe();
        return EditResult{EditStatus::ReadOnly,
                          descriptor ? descriptor->revision : 0,
                          descriptor ? descriptor->size : 0,
                          {}};
    }
    try {
        EditResult result = (*session)->applyTransaction(std::move(request));
        if (result.accepted() &&
            !adoptExternalSnapshot(result.committedSnapshot, result.revision, result.size)) {
            result.status = EditStatus::CommittedSnapshotUnavailable;
            result.committedSnapshot.reset();
        }
        return result;
    } catch (...) {
        const auto descriptor = m_lastExternalDescriptor;
        invalidateExternalSnapshot();
        return EditResult{EditStatus::CommittedSnapshotUnavailable,
                          descriptor ? descriptor->revision : 0,
                          descriptor ? descriptor->size : 0,
                          {}};
    }
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
        return EditResult{EditStatus::ReadOnly,
                          descriptor ? descriptor->revision : 0,
                          descriptor ? descriptor->size : 0,
                          {}};
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
        return EditResult{EditStatus::ReadOnly,
                          descriptor ? descriptor->revision : 0,
                          descriptor ? descriptor->size : 0,
                          {}};
    }
    return owned->replace(
        std::move(text), expectedRevision);
}

std::optional<char16_t> BufferStorage::codeUnitAt(const std::size_t offset) const noexcept {
    if (const auto* owned = ownedText()) {
        return offset < owned->size() ? std::optional<char16_t>(owned->text()[offset])
                                      : std::nullopt;
    }
    const auto descriptor = describe();
    if (!descriptor || offset >= descriptor->size) {
        return std::nullopt;
    }
    if (isExternalSession() && m_snapshot && m_scalarCacheCapacity == 0) {
        try {
            const auto value = m_snapshot->codeUnitAt(offset);
            if (!value) {
                markExternalReadFault();
            }
            return value;
        } catch (...) {
            markExternalReadFault();
            return std::nullopt;
        }
    }
    if (m_scalarCacheRevision == descriptor->revision && offset >= m_scalarCacheOffset &&
        offset - m_scalarCacheOffset < m_scalarCache.size()) {
        return m_scalarCache[offset - m_scalarCacheOffset];
    }
    const std::size_t capacity = std::max<std::size_t>(1, m_scalarCacheCapacity);
    const RangeRead range = read(
        RangeRequest{offset, std::min(capacity, descriptor->size - offset), descriptor->revision});
    if (!range.ok() || range.text.empty()) {
        return std::nullopt;
    }
    m_scalarCacheRevision = descriptor->revision;
    m_scalarCacheOffset = offset;
    m_scalarCache = range.text;
    return m_scalarCache.front();
}

std::optional<std::u16string> BufferStorage::readExact(const std::size_t offset,
                                                       const std::size_t length) const noexcept {
    const auto descriptor = describe();
    if (!descriptor || offset > descriptor->size || length > descriptor->size - offset) {
        return std::nullopt;
    }
    std::u16string text;
    try {
        text.reserve(length);
    } catch (...) {
        return std::nullopt;
    }
    std::size_t position = offset;
    while (text.size() < length) {
        const RangeRead range =
            read(RangeRequest{position, length - text.size(), descriptor->revision});
        if (!range.ok() || range.text.empty()) {
            if (range.ok() && range.text.empty() && isExternalSession()) {
                markExternalReadFault();
            }
            return std::nullopt;
        }
        text.append(range.text);
        position += range.text.size();
    }
    return text;
}

std::optional<std::size_t> BufferStorage::lineCount() const noexcept {
    if (!isExternalSession() || m_externalReadFault || !m_snapshot) {
        return std::nullopt;
    }
    try {
        const std::size_t count = m_snapshot->lineCount();
        if (count == 0) {
            markExternalReadFault();
            return std::nullopt;
        }
        return count;
    } catch (...) {
        markExternalReadFault();
        return std::nullopt;
    }
}

std::optional<std::size_t> BufferStorage::lineStart(const std::size_t line) const noexcept {
    if (!isExternalSession() || m_externalReadFault || !m_snapshot) {
        return std::nullopt;
    }
    try {
        const std::size_t count = m_snapshot->lineCount();
        if (count == 0) {
            markExternalReadFault();
            return std::nullopt;
        }
        if (line >= count) {
            return std::nullopt;
        }
        const auto start = m_snapshot->lineStart(line);
        if (!start || (line == 0 && *start != 0) ||
            (m_lastExternalDescriptor && *start > m_lastExternalDescriptor->size)) {
            markExternalReadFault();
            return std::nullopt;
        }
        if (line > 0) {
            const auto previous = m_snapshot->lineStart(line - 1);
            if (!previous || *previous >= *start) {
                markExternalReadFault();
                return std::nullopt;
            }
        }
        return start;
    } catch (...) {
        markExternalReadFault();
        return std::nullopt;
    }
}

std::optional<std::size_t> BufferStorage::lineForOffset(const std::size_t offset) const noexcept {
    if (!isExternalSession() || m_externalReadFault || !m_snapshot) {
        return std::nullopt;
    }
    try {
        if (!m_lastExternalDescriptor || offset > m_lastExternalDescriptor->size) {
            return std::nullopt;
        }
        const std::size_t count = m_snapshot->lineCount();
        const auto line = m_snapshot->lineForOffset(offset);
        if (count == 0 || !line || *line >= count) {
            markExternalReadFault();
            return std::nullopt;
        }
        const auto start = m_snapshot->lineStart(*line);
        const auto next =
            *line + 1 < count ? m_snapshot->lineStart(*line + 1) : std::optional<std::size_t>{};
        if (!start || *start > offset || (next && *next <= offset) ||
            (!next && *line + 1 < count)) {
            markExternalReadFault();
            return std::nullopt;
        }
        return line;
    } catch (...) {
        markExternalReadFault();
        return std::nullopt;
    }
}

std::optional<SessionHistory> BufferStorage::externalHistory() const noexcept {
    if (m_externalReadFault || !m_snapshot) {
        return std::nullopt;
    }
    const auto* session = std::get_if<std::shared_ptr<IEditableTextSession>>(&m_backing);
    if (session == nullptr || !*session) {
        return std::nullopt;
    }
    try {
        return (*session)->history();
    } catch (...) {
        markExternalReadFault();
        return std::nullopt;
    }
}

std::optional<TextSelection> BufferStorage::externalSelection() const noexcept {
    if (m_externalReadFault || !m_snapshot) {
        return std::nullopt;
    }
    const auto* session = std::get_if<std::shared_ptr<IEditableTextSession>>(&m_backing);
    if (session == nullptr || !*session) {
        return std::nullopt;
    }
    try {
        return (*session)->selection();
    } catch (...) {
        markExternalReadFault();
        return std::nullopt;
    }
}

bool BufferStorage::setExternalSelection(TextSelection selection) {
    if (m_externalReadFault || !m_snapshot) {
        return false;
    }
    auto* session = std::get_if<std::shared_ptr<IEditableTextSession>>(&m_backing);
    try {
        return session != nullptr && *session && (*session)->setSelection(selection);
    } catch (...) {
        invalidateExternalSnapshot();
        return false;
    }
}

HistoryReplayResult BufferStorage::replayExternalHistory(const std::size_t count, const bool redo) {
    if (m_externalReadFault || !m_snapshot) {
        return HistoryReplayResult{HistoryReplayStatus::Unavailable,
                                   m_lastExternalDescriptor ? m_lastExternalDescriptor->revision
                                                            : 0,
                                   m_lastExternalDescriptor ? m_lastExternalDescriptor->size : 0,
                                   {},
                                   std::nullopt,
                                   std::nullopt,
                                   {}};
    }
    auto* session = std::get_if<std::shared_ptr<IEditableTextSession>>(&m_backing);
    if (session == nullptr || !*session) {
        return {};
    }
    try {
        HistoryReplayResult result = redo ? (*session)->redo(count) : (*session)->undo(count);
        if (result.accepted() &&
            !adoptExternalSnapshot(result.committedSnapshot, result.revision, result.size)) {
            result.status = HistoryReplayStatus::CommittedSnapshotUnavailable;
            result.committedSnapshot.reset();
        }
        return result;
    } catch (...) {
        const auto descriptor = m_lastExternalDescriptor;
        invalidateExternalSnapshot();
        return HistoryReplayResult{HistoryReplayStatus::CommittedSnapshotUnavailable,
                                   descriptor ? descriptor->revision : 0,
                                   descriptor ? descriptor->size : 0,
                                   {},
                                   std::nullopt,
                                   std::nullopt,
                                   {}};
    }
}

bool BufferStorage::resetExternalHistory() {
    if (m_externalReadFault || !m_snapshot) {
        return false;
    }
    auto* session = std::get_if<std::shared_ptr<IEditableTextSession>>(&m_backing);
    try {
        return session != nullptr && *session && (*session)->resetHistory();
    } catch (...) {
        invalidateExternalSnapshot();
        return false;
    }
}

bool BufferStorage::setExternalModified(const bool modified) {
    if (m_externalReadFault || !m_snapshot) {
        return false;
    }
    auto* session = std::get_if<std::shared_ptr<IEditableTextSession>>(&m_backing);
    try {
        return session != nullptr && *session && (*session)->setModified(modified);
    } catch (...) {
        invalidateExternalSnapshot();
        return false;
    }
}

bool BufferStorage::adoptExternalSnapshot(std::shared_ptr<const ITextSnapshot> snapshot,
                                          const Revision expectedRevision,
                                          const std::size_t expectedSize) noexcept {
    try {
        if (!snapshot) {
            invalidateExternalSnapshot();
            return false;
        }
        const Descriptor current = m_snapshot ? m_snapshot->describe() : Descriptor{};
        const Descriptor replacement = snapshot->describe();
        if (!validDescriptor(replacement) || !replacement.modalEditingLfOnly ||
            replacement.identity != current.identity || replacement.revision != expectedRevision ||
            replacement.size != expectedSize) {
            invalidateExternalSnapshot();
            return false;
        }
        m_snapshot = std::move(snapshot);
        m_lastExternalDescriptor = replacement;
        m_externalReadFault = false;
        m_scalarCache.clear();
        m_scalarCacheRevision = 0;
        m_scalarCacheOffset = 0;
        return true;
    } catch (...) {
        invalidateExternalSnapshot();
        return false;
    }
}

void BufferStorage::invalidateExternalSnapshot() noexcept {
    m_snapshot.reset();
    m_externalReadFault = true;
    m_scalarCache.clear();
    m_scalarCacheRevision = 0;
    m_scalarCacheOffset = 0;
}

void BufferStorage::invalidateExternalProjection() noexcept {
    invalidateExternalSnapshot();
}

void BufferStorage::markExternalReadFault() const noexcept {
    if (!isExternalSession()) {
        return;
    }
    m_externalReadFault = true;
    m_scalarCache.clear();
    m_scalarCacheRevision = 0;
    m_scalarCacheOffset = 0;
}

} // namespace vkui::buffer
