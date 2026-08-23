#include "VkCoreInternal.h"

#include <vkui/core/VkDiagnostics.h>

namespace vkui::vk {

BufferId VkCore::synchronizeBuffer(
    const ViewId view,
    std::string path,
    std::u16string text,
    Cursor cursor)
{
    vkui::DiagnosticSpan syncSpan(
        QStringLiteral("vkcore.buffer"),
        QStringLiteral("synchronize"),
        {
            {QStringLiteral("view"), static_cast<qint64>(view)},
            {QStringLiteral("path"), QString::fromStdString(path)},
            {QStringLiteral("text_units"),
             static_cast<qint64>(text.size())},
        });
    const auto foundView = m_impl->views.find(view);
    if (foundView == m_impl->views.end() || path.empty()) {
        syncSpan.fail(QStringLiteral("invalid view or path"));
        return 0;
    }
    BufferId id = 0;
    const auto foundPath = m_impl->paths.find(path);
    if (foundPath == m_impl->paths.end()) {
        id = m_impl->nextBuffer++;
        Implementation::Buffer buffer;
        buffer.id = id;
        buffer.path = std::move(path);
        buffer.storage =
            vkui::buffer::BufferStorage::owned(
                vkui::buffer::Identity{
                    "vkcore:" + std::to_string(id)},
                std::move(text),
                1,
                std::numeric_limits<std::size_t>::max());
        buffer.lineStarts = lineStarts(buffer.storage.ownedText()->text());
        m_impl->paths.emplace(
            buffer.path, id);
        m_impl->buffers.emplace(id, std::move(buffer));
        m_impl->bufferOrder.push_back(id);
    } else {
        id = foundPath->second;
        auto &buffer = m_impl->buffers.at(id);
        if (!buffer.storage.isOwned()) {
            syncSpan.fail(QStringLiteral("provider buffer path conflict"));
            return 0;
        }
        if (buffer.text() != text) {
            if (!buffer.replaceText(std::move(text))) {
                syncSpan.fail(QStringLiteral("buffer storage rejected text"));
                return 0;
            }
            buffer.lineStarts = lineStarts(buffer.storage.ownedText()->text());
            buffer.displayLines.clear();
            m_impl->resetUndoHistory(buffer, id);
        }
    }
    syncSpan.annotate(QStringLiteral("buffer"), static_cast<qint64>(id));
    if (!attachBuffer(view, id, cursor)) {
        syncSpan.fail(QStringLiteral("buffer could not attach"));
        return 0;
    }
    return id;
}

BufferRegistrationResult VkCore::registerProviderBuffer(
    std::string path,
    std::shared_ptr<const vkui::buffer::IRangeProvider> provider,
    const std::size_t maximumReadLength)
{
    vkui::DiagnosticSpan registrationSpan(
        QStringLiteral("vkcore.buffer"),
        QStringLiteral("register-provider"),
        {
            {QStringLiteral("path"), QString::fromStdString(path)},
            {QStringLiteral("maximum_read_length"),
             static_cast<qint64>(maximumReadLength)},
        });
    if (path.empty()) {
        registrationSpan.fail(QStringLiteral("invalid path"));
        return BufferRegistrationResult{
            0, BufferRegistrationStatus::InvalidPath};
    }
    auto storage = vkui::buffer::BufferStorage::provider(
        std::move(provider), maximumReadLength);
    const auto descriptor = storage.describe();
    if (!descriptor) {
        registrationSpan.fail(QStringLiteral("invalid provider"));
        return BufferRegistrationResult{
            0, BufferRegistrationStatus::InvalidProvider};
    }

    const auto existingPath = m_impl->paths.find(path);
    if (existingPath != m_impl->paths.end()) {
        const auto &existing =
            m_impl->buffers.at(existingPath->second);
        const auto existingDescriptor =
            existing.storage.describe();
        registrationSpan.annotate(
            QStringLiteral("buffer"),
            static_cast<qint64>(existingPath->second));
        return BufferRegistrationResult{
            existingPath->second,
            existingDescriptor
                    && existingDescriptor->identity
                        == descriptor->identity
                ? BufferRegistrationStatus::AlreadyRegistered
                : BufferRegistrationStatus::PathConflict};
    }
    for (const auto &[id, buffer] : m_impl->buffers) {
        const auto existingDescriptor =
            buffer.storage.describe();
        if (existingDescriptor
            && existingDescriptor->identity
                == descriptor->identity) {
            return BufferRegistrationResult{
                id,
                BufferRegistrationStatus::AlreadyRegistered};
        }
    }

    const BufferId id = m_impl->nextBuffer++;
    Implementation::Buffer buffer;
    buffer.id = id;
    buffer.path = std::move(path);
    buffer.storage = std::move(storage);
    buffer.readOnly = true;
    m_impl->paths.emplace(buffer.path, id);
    m_impl->buffers.emplace(id, std::move(buffer));
    m_impl->bufferOrder.push_back(id);
    registrationSpan.annotate(
        QStringLiteral("buffer"), static_cast<qint64>(id));
    return BufferRegistrationResult{
        id, BufferRegistrationStatus::Created};
}

BufferRegistrationResult VkCore::registerExternalSessionBuffer(
    std::string path, std::shared_ptr<vkui::buffer::IEditableTextSession> session,
    const std::size_t maximumReadLength, const std::size_t scalarCacheCapacity) {
    if (path.empty()) {
        return BufferRegistrationResult{0, BufferRegistrationStatus::InvalidPath};
    }
    auto storage = vkui::buffer::BufferStorage::externalSession(
        std::move(session), maximumReadLength, scalarCacheCapacity);
    const auto descriptor = storage.describe();
    if (!descriptor) {
        return BufferRegistrationResult{0, BufferRegistrationStatus::InvalidProvider};
    }
    if (!descriptor->modalEditingLfOnly) {
        return BufferRegistrationResult{0, BufferRegistrationStatus::UnsupportedLineEndings};
    }
    const auto existingPath = m_impl->paths.find(path);
    if (existingPath != m_impl->paths.end()) {
        const auto existingDescriptor = m_impl->buffers.at(existingPath->second).storage.describe();
        return BufferRegistrationResult{existingPath->second,
                                        existingDescriptor &&
                                                existingDescriptor->identity == descriptor->identity
                                            ? BufferRegistrationStatus::AlreadyRegistered
                                            : BufferRegistrationStatus::PathConflict};
    }
    for (const auto& [id, buffer] : m_impl->buffers) {
        const auto existingDescriptor = buffer.storage.describe();
        if (existingDescriptor && existingDescriptor->identity == descriptor->identity) {
            return BufferRegistrationResult{id, BufferRegistrationStatus::AlreadyRegistered};
        }
    }

    const BufferId id = m_impl->nextBuffer++;
    Implementation::Buffer buffer;
    buffer.id = id;
    buffer.path = std::move(path);
    buffer.storage = std::move(storage);
    buffer.readOnly = !descriptor->editable;
    buffer.undo.reset();
    m_impl->paths.emplace(buffer.path, id);
    auto [inserted, created] = m_impl->buffers.emplace(id, std::move(buffer));
    if (!created) {
        m_impl->paths.erase(inserted->second.path);
        return BufferRegistrationResult{0, BufferRegistrationStatus::InvalidProvider};
    }
    inserted->second.lineStarts.bindExternal(&inserted->second.storage);
    m_impl->bufferOrder.push_back(id);
    return BufferRegistrationResult{id, BufferRegistrationStatus::Created};
}

bool VkCore::attachBuffer(
    const WindowId window,
    const BufferId id,
    Cursor cursor)
{
    return attachBufferData(window, id, cursor)
        == BufferAttachStatus::Attached;
}

BufferAttachStatus VkCore::attachBufferData(
    const WindowId window,
    const BufferId id,
    Cursor cursor)
{
    const auto foundView = m_impl->views.find(window);
    const auto foundBuffer = m_impl->buffers.find(id);
    if (foundView == m_impl->views.end()) {
        vkui::writeDiagnostic(
            vkui::DiagnosticLevel::Error,
            QStringLiteral("vkcore.buffer"),
            QStringLiteral("attach.rejected"),
            QStringLiteral("Unknown window"),
            QJsonObject{
                {QStringLiteral("window"), static_cast<qint64>(window)},
                {QStringLiteral("buffer"), static_cast<qint64>(id)},
            });
        return BufferAttachStatus::UnknownWindow;
    }
    if (foundBuffer == m_impl->buffers.end()) {
        vkui::writeDiagnostic(
            vkui::DiagnosticLevel::Error,
            QStringLiteral("vkcore.buffer"),
            QStringLiteral("attach.rejected"),
            QStringLiteral("Unknown buffer"),
            QJsonObject{
                {QStringLiteral("window"), static_cast<qint64>(window)},
                {QStringLiteral("buffer"), static_cast<qint64>(id)},
            });
        return BufferAttachStatus::UnknownBuffer;
    }
    if (foundBuffer->second.storage.isProviderBacked()) {
        vkui::writeDiagnostic(
            vkui::DiagnosticLevel::Debug, QStringLiteral("vkcore.buffer"),
            QStringLiteral("attach.requires-projection"),
            QStringLiteral("Read-only provider data requires a resident projection"),
            QJsonObject{
                {QStringLiteral("window"), static_cast<qint64>(window)},
                {QStringLiteral("buffer"), static_cast<qint64>(id)},
            });
        return BufferAttachStatus::RequiresResidentTextProjection;
    }
    if (window == m_impl->activeView
        && m_impl->baseMode == Mode::Replace
        && foundView->second.buffer != id) {
        m_impl->finishReplaceMode(
            nullptr, window, false);
    }

    auto &targetView = foundView->second;
    if (targetView.buffer != 0
        && targetView.buffer != id) {
        targetView.alternateBuffer = targetView.buffer;
        const auto previous = m_impl->buffers.find(
            targetView.buffer);
        const auto previousCursor = targetView.cursors.find(
            targetView.buffer);
        if (previous != m_impl->buffers.end()
            && previousCursor != targetView.cursors.end()) {
            previous->second.lastCursorMark =
                m_impl->offset(
                    previous->second,
                    m_impl->clampCursor(
                        previous->second,
                        previousCursor->second,
                        false));
        }
    }
    targetView.buffer = id;
    targetView.selectionAnchorOffset.reset();
    const auto &buffer = foundBuffer->second;
    targetView.changeIndex = buffer.changeList.size();
    cursor = m_impl->clampCursor(
        buffer,
        cursor,
        !m_impl->enabled
            || m_impl->baseMode == Mode::Insert
            || m_impl->baseMode == Mode::Replace);
    targetView.cursors[id] = cursor;
    targetView.displayColumns[id] =
        m_impl->displayColumnForBufferColumn(
            buffer, cursor.line, cursor.column);
    targetView.preferredColumn.reset();
    targetView.preferredDisplayRowColumn.reset();
    targetView.topline = std::min(
        targetView.topline,
        buffer.lineStarts.size() - 1);
    if (window == m_impl->activeView
        && (m_impl->baseMode == Mode::Insert
            || m_impl->baseMode == Mode::Replace)) {
        m_impl->continueInsertUndoBlock(window);
    }
    vkui::writeDiagnostic(
        vkui::DiagnosticLevel::Debug,
        QStringLiteral("vkcore.buffer"),
        QStringLiteral("attach.complete"),
        QStringLiteral("Authoritative buffer attached to window"),
        QJsonObject{
            {QStringLiteral("window"), static_cast<qint64>(window)},
            {QStringLiteral("buffer"), static_cast<qint64>(id)},
            {QStringLiteral("path"),
             QString::fromStdString(foundBuffer->second.path)},
        });
    return BufferAttachStatus::Attached;
}

bool VkCore::replaceBufferText(
    const BufferId id,
    std::u16string text,
    const Cursor cursor)
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.end()
        || !found->second.storage.isOwned()) {
        return false;
    }
    auto &buffer = found->second;
    if (buffer.text() != text) {
        if (!buffer.replaceText(std::move(text))) {
            return false;
        }
        buffer.lineStarts = lineStarts(buffer.storage.ownedText()->text());
        buffer.displayLines.clear();
        m_impl->resetUndoHistory(buffer, id);
    }
    for (auto &[viewId, view] : m_impl->views) {
        if (view.buffer == id) {
            Cursor preserved =
                viewId == m_impl->activeView
                ? cursor
                : view.cursors[id];
            preserved = m_impl->clampCursor(
                buffer,
                preserved,
                !m_impl->enabled
                    || ((m_impl->baseMode == Mode::Insert
                         || m_impl->baseMode == Mode::Replace)
                        && viewId == m_impl->activeView));
            view.cursors[id] = preserved;
            view.selectionAnchorOffset.reset();
            view.displayColumns[id] =
                m_impl->displayColumnForBufferColumn(
                    buffer,
                    preserved.line,
                    preserved.column);
            view.preferredColumn.reset();
            view.preferredDisplayRowColumn.reset();
            view.topline = std::min(
                view.topline,
                buffer.lineStarts.size() - 1);
        }
    }
    if (m_impl->baseMode == Mode::Insert
        || m_impl->baseMode == Mode::Replace) {
        m_impl->continueInsertUndoBlock(
            m_impl->activeView);
    }
    return true;
}

bool VkCore::applyExternalEdit(
    const ViewId viewId,
    const std::size_t editOffset,
    const std::size_t removed,
    std::u16string inserted,
    const Cursor cursor)
{
    // Any host/IME edit starts a new completion context. Core-originating
    // completion edits call mutateBuffer() directly and do not pass here.
    m_impl->keywordCompletion.reset();
    const auto foundView = m_impl->views.find(viewId);
    if (foundView == m_impl->views.end()
        || foundView->second.buffer == 0) {
        return false;
    }
    const BufferId bufferId =
        foundView->second.buffer;
    const auto foundBuffer =
        m_impl->buffers.find(bufferId);
    if (foundBuffer == m_impl->buffers.end() || foundBuffer->second.authorityDesynchronized ||
        foundBuffer->second.storage.externalReadFaulted() ||
        editOffset > foundBuffer->second.text().size() ||
        removed > foundBuffer->second.text().size() - editOffset) {
        return false;
    }
    if (m_impl->baseMode == Mode::Insert
        && viewId == m_impl->activeView) {
        m_impl->continueInsertUndoBlock(viewId);
        m_impl->recordExternalInsertEdit(
            viewId,
            editOffset,
            removed,
            inserted);
    }
    return m_impl->mutateBuffer(
        nullptr,
        bufferId,
        editOffset,
        editOffset + removed,
        std::move(inserted),
        std::pair<ViewId, Cursor>{viewId, cursor});
}

bool VkCore::applyExternalEditAtOffset(
    const ViewId viewId,
    const std::size_t editOffset,
    const std::size_t removed,
    std::u16string inserted,
    const std::size_t cursorOffset)
{
    return applyExternalEditWithSelectionAtOffsets(
        viewId,
        editOffset,
        removed,
        std::move(inserted),
        cursorOffset,
        cursorOffset);
}

bool VkCore::applyExternalEditWithSelectionAtOffsets(
    const ViewId viewId,
    const std::size_t editOffset,
    const std::size_t removed,
    std::u16string inserted,
    const std::size_t selectionAnchorOffset,
    const std::size_t cursorOffset)
{
    m_impl->keywordCompletion.reset();
    const auto foundView = m_impl->views.find(viewId);
    if (foundView == m_impl->views.end()
        || foundView->second.buffer == 0) {
        return false;
    }
    const BufferId bufferId = foundView->second.buffer;
    const auto foundBuffer = m_impl->buffers.find(bufferId);
    if (foundBuffer == m_impl->buffers.end() || foundBuffer->second.authorityDesynchronized ||
        foundBuffer->second.storage.externalReadFaulted() ||
        editOffset > foundBuffer->second.text().size() ||
        removed > foundBuffer->second.text().size() - editOffset) {
        return false;
    }
    if (m_impl->baseMode == Mode::Insert
        && viewId == m_impl->activeView) {
        m_impl->continueInsertUndoBlock(viewId);
        m_impl->recordExternalInsertEdit(
            viewId, editOffset, removed, inserted);
    }
    return m_impl->mutateBuffer(
        nullptr,
        bufferId,
        editOffset,
        editOffset + removed,
        std::move(inserted),
        std::nullopt,
        true,
        true,
        VkCore::Implementation::AuthoritativeSelectionOffsetState{
            viewId,
            selectionAnchorOffset,
            cursorOffset});
}

bool VkCore::applyExternalEditBatchWithSelectionAtOffsets(
    const ViewId viewId, std::vector<vkui::buffer::TransactionEdit> edits,
    const std::size_t selectionBeforeAnchor, const std::size_t selectionBeforeCursor,
    const std::size_t selectionAfterAnchor, const std::size_t selectionAfterCursor,
    const std::optional<std::uint64_t> group) {
    const auto foundView = m_impl->views.find(viewId);
    if (foundView == m_impl->views.end() || foundView->second.buffer == 0) {
        return false;
    }
    return m_impl->mutateExternalBatch(
        nullptr, foundView->second.buffer, std::move(edits),
        vkui::buffer::TextSelection{selectionBeforeAnchor, selectionBeforeCursor},
        vkui::buffer::TextSelection{selectionAfterAnchor, selectionAfterCursor}, viewId, group,
        false);
}

std::optional<BufferHistorySnapshot> VkCore::bufferHistory(
    const BufferId id) const
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.end()) {
        return std::nullopt;
    }
    if (found->second.storage.isExternalSession()) {
        const auto history = found->second.storage.externalHistory();
        if (!history) {
            return std::nullopt;
        }
        return BufferHistorySnapshot{id, history->current, history->clean, history->canUndo,
                                     history->canRedo};
    }
    const Implementation::UndoHistory& history = *found->second.undo;
    const Implementation::UndoNode &current = history.nodes[history.current];
    BufferHistorySnapshot snapshot;
    snapshot.id = id;
    snapshot.current = static_cast<std::uint64_t>(history.current);
    if (history.clean) {
        snapshot.clean = static_cast<std::uint64_t>(*history.clean);
    }
    snapshot.canUndo = history.current != 0;
    snapshot.canRedo = current.preferredChild
        != Implementation::UndoNode::noNode;
    return snapshot;
}

bool VkCore::resetBufferHistory(const BufferId id)
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.end()) {
        return false;
    }
    if (found->second.storage.isExternalSession()) {
        return found->second.storage.resetExternalHistory();
    }
    if (!found->second.storage.isOwned()) {
        return false;
    }
    m_impl->resetUndoHistory(found->second, id);
    return true;
}

bool VkCore::setBufferModified(
    const BufferId id,
    const bool modified)
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.end()) {
        return false;
    }
    if (found->second.storage.isExternalSession()) {
        return found->second.storage.setExternalModified(modified);
    }
    found->second.undo->clean =
        modified ? std::nullopt : std::optional<std::size_t>(found->second.undo->current);
    return true;
}

DispatchResult VkCore::undo(
    const WindowId window,
    const std::size_t count)
{
    return m_impl->replayPublicHistory(
        window, count, false);
}

DispatchResult VkCore::redo(
    const WindowId window,
    const std::size_t count)
{
    return m_impl->replayPublicHistory(
        window, count, true);
}

} // namespace vkui::vk
