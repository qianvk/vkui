#include "VkCoreInternal.h"

#include <vkui/core/VkDiagnostics.h>

namespace vkui::vk {

bool VkCore::setViewCursor(
    const ViewId id,
    Cursor cursor)
{
    const auto foundView = m_impl->views.find(id);
    if (foundView == m_impl->views.end()
        || foundView->second.buffer == 0) {
        return false;
    }
    auto foundBuffer = m_impl->buffers.find(
        foundView->second.buffer);
    if (foundBuffer == m_impl->buffers.end()) {
        return false;
    }
    cursor = m_impl->clampCursor(
        foundBuffer->second,
        cursor,
        !m_impl->enabled
            || ((m_impl->baseMode == Mode::Insert
                 || m_impl->baseMode == Mode::Replace)
                && id == m_impl->activeView));
    foundView->second.cursors[foundView->second.buffer] = cursor;
    foundView->second.displayColumns[foundView->second.buffer] =
        m_impl->displayColumnForBufferColumn(
            foundBuffer->second,
            cursor.line,
            cursor.column);
    foundView->second.preferredColumn.reset();
    foundView->second.preferredDisplayRowColumn.reset();
    if (m_impl->keywordCompletion
        && m_impl->keywordCompletion->window == id) {
        m_impl->keywordCompletion.reset();
    }
    return true;
}

bool VkCore::jumpViewCursor(const ViewId id, Cursor cursor)
{
    const auto foundView = m_impl->views.find(id);
    if (foundView == m_impl->views.end()
        || foundView->second.buffer == 0) {
        return false;
    }
    auto foundBuffer = m_impl->buffers.find(foundView->second.buffer);
    if (foundBuffer == m_impl->buffers.end()) {
        return false;
    }
    Implementation::View &view = foundView->second;
    const Cursor origin = m_impl->clampCursor(
        foundBuffer->second,
        view.cursors[view.buffer],
        false);
    cursor = m_impl->clampCursor(foundBuffer->second, cursor, false);
    if (origin != cursor) {
        m_impl->recordJump(
            view,
            Implementation::Location{
                view.buffer,
                m_impl->offset(foundBuffer->second, origin)},
            Implementation::Location{
                view.buffer,
                m_impl->offset(foundBuffer->second, cursor)});
    }
    view.cursors[view.buffer] = cursor;
    view.displayColumns[view.buffer] =
        m_impl->displayColumnForBufferColumn(
            foundBuffer->second, cursor.line, cursor.column);
    view.preferredColumn.reset();
    view.preferredDisplayRowColumn.reset();
    return true;
}

std::optional<std::size_t> VkCore::bufferOffset(
    const BufferId id,
    Cursor cursor) const
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.cend()
        || !found->second.storage.isOwned()) {
        return std::nullopt;
    }
    cursor = m_impl->clampCursor(
        found->second, cursor, true);
    return m_impl->offset(found->second, cursor);
}

std::optional<Cursor> VkCore::cursorForBufferOffset(
    const BufferId id,
    const std::size_t rawOffset) const
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.cend()
        || !found->second.storage.isOwned()) {
        return std::nullopt;
    }
    const std::size_t bounded = std::min(
        rawOffset, found->second.text().size());
    return m_impl->cursorAtOffset(found->second, bounded);
}

bool VkCore::setBufferReadOnly(
    const BufferId id,
    const bool readOnly)
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.end()
        || !found->second.storage.isOwned()) {
        return false;
    }
    found->second.readOnly = readOnly;
    return true;
}

bool VkCore::setBufferTabStop(
    const BufferId id,
    const std::size_t columns)
{
    if (columns == 0 || columns > 9999) {
        return false;
    }
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.end()
        || !found->second.storage.isOwned()) {
        return false;
    }
    auto &buffer = found->second;
    if (buffer.tabStop == columns) {
        return true;
    }
    buffer.tabStop = columns;
    buffer.displayLines.clear();
    for (auto &[windowId, view] : m_impl->views) {
        static_cast<void>(windowId);
        if (view.buffer != id) {
            continue;
        }
        const Cursor cursor = view.cursors[id];
        view.displayColumns[id] =
            m_impl->displayColumnForBufferColumn(
                buffer, cursor.line, cursor.column);
        view.preferredColumn.reset();
        view.preferredDisplayRowColumn.reset();
    }
    if (m_impl->visualSelection
        && m_impl->visualSelection->buffer == id) {
        m_impl->visualSelection->anchorDisplayColumn =
            m_impl->displayColumnForBufferColumn(
                buffer,
                m_impl->visualSelection->anchor.line,
                m_impl->visualSelection->anchor.column);
    }
    return true;
}

std::optional<DisplayLayoutCacheStats>
VkCore::displayLayoutCacheStats(const BufferId id) const
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.cend()
        || !found->second.storage.isOwned()) {
        return std::nullopt;
    }
    DisplayLayoutCacheStats stats;
    stats.cachedLines = found->second.displayLines.size();
    for (const auto &[line, layout] :
         found->second.displayLines) {
        static_cast<void>(line);
        stats.specialCells += layout.specialCells.size();
        stats.allocatedCellCapacity +=
            layout.specialCells.capacity();
    }
    return stats;
}

std::optional<DisplayPosition> VkCore::displayPosition(
    const BufferId id,
    const Cursor cursor) const
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.cend()
        || !found->second.storage.isOwned()
        || cursor.line >= found->second.lineStarts.size()
        || m_impl->clampCursor(
               found->second, cursor, true)
            != cursor) {
        return std::nullopt;
    }
    return DisplayPosition{
        cursor,
        m_impl->displayColumnForBufferColumn(
            found->second,
            cursor.line,
            cursor.column)};
}

std::optional<DisplayPosition> VkCore::displayPositionForColumn(
    const BufferId id,
    const std::size_t line,
    std::size_t absoluteDisplayColumn,
    const DisplayColumnMode mode) const
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.cend()
        || !found->second.storage.isOwned()
        || line >= found->second.lineStarts.size()) {
        return std::nullopt;
    }
    const Implementation::Buffer &buffer = found->second;
    if (mode == DisplayColumnMode::Character) {
        const Cursor cursor =
            m_impl->normalCursorForDisplayColumn(
                buffer, line, absoluteDisplayColumn);
        return DisplayPosition{
            cursor,
            m_impl->displayColumnForBufferColumn(
                buffer, line, cursor.column)};
    }
    if (mode != DisplayColumnMode::VisualBlock) {
        return std::nullopt;
    }

    if (m_impl->virtualEdit != VirtualEditMode::Block) {
        absoluteDisplayColumn = std::min(
            absoluteDisplayColumn,
            m_impl->displayLine(buffer, line).width);
    }
    return m_impl->displayPositionForColumn(
        buffer, line, absoluteDisplayColumn);
}

std::optional<BufferId> VkCore::bufferForPath(
    const std::string_view path) const
{
    if (path.empty()) {
        return std::nullopt;
    }
    const auto found = m_impl->paths.find(path);
    return found == m_impl->paths.end()
        ? std::nullopt
        : std::optional<BufferId>(found->second);
}

bool VkCore::hasAttachedBufferAtOrBelowPath(
    const std::string_view rawPath) const
{
    const std::string_view path =
        normalizedPathRoot(rawPath);
    if (path.empty()) {
        return false;
    }
    for (const auto &[id, buffer] : m_impl->buffers) {
        if (pathAtOrBelow(buffer.path, path)
            && m_impl->bufferAttached(id)) {
            return true;
        }
    }
    return false;
}

bool VkCore::rebindBufferPath(
    const BufferId id,
    std::string newPath,
    const bool replaceConflictingBuffer)
{
    const QString oldPathText = [&] {
        const auto found = m_impl->buffers.find(id);
        return found == m_impl->buffers.end()
            ? QString()
            : QString::fromStdString(found->second.path);
    }();
    if (newPath.empty()) {
        return false;
    }
    const auto foundBuffer = m_impl->buffers.find(id);
    if (foundBuffer == m_impl->buffers.end()) {
        return false;
    }
    const auto existing = m_impl->paths.find(newPath);
    if (existing != m_impl->paths.end()) {
        if (existing->second == id) {
            return true;
        }
        if (!replaceConflictingBuffer) {
            return false;
        }
        if (m_impl->bufferAttached(existing->second)) {
            return false;
        }
        (void)m_impl->eraseBuffers(
            std::unordered_set<BufferId>{
                existing->second});
    }

    auto &buffer = m_impl->buffers.at(id);
    const std::string oldPath = buffer.path;
    m_impl->paths.emplace(newPath, id);
    m_impl->paths.erase(oldPath);
    buffer.path = std::move(newPath);
    vkui::writeDiagnostic(
        vkui::DiagnosticLevel::Info,
        QStringLiteral("vkcore.buffer"),
        QStringLiteral("path.rebound"),
        QStringLiteral("Buffer path identity updated"),
        QJsonObject{
            {QStringLiteral("buffer"), static_cast<qint64>(id)},
            {QStringLiteral("old_path"), oldPathText},
            {QStringLiteral("new_path"),
             QString::fromStdString(buffer.path)},
        });
    return true;
}

bool VkCore::rebindBuffersUnderPath(
    const std::string_view rawOldRoot,
    const std::string_view rawNewRoot,
    std::size_t *const rebound,
    const bool replaceConflictingBuffers)
{
    if (rebound != nullptr) {
        *rebound = 0;
    }
    const std::string oldRoot(
        normalizedPathRoot(rawOldRoot));
    const std::string newRoot(
        normalizedPathRoot(rawNewRoot));
    if (oldRoot.empty() || newRoot.empty()) {
        return false;
    }
    if (oldRoot == newRoot) {
        return true;
    }

    struct PathChange final
    {
        BufferId id = 0;
        std::string oldPath;
        std::string newPath;
    };
    std::vector<PathChange> changes;
    changes.reserve(m_impl->buffers.size());
    std::unordered_set<BufferId> affected;
    affected.reserve(m_impl->buffers.size());
    std::unordered_set<BufferId> displaced;
    displaced.reserve(m_impl->buffers.size());
    std::unordered_set<std::string, StringViewHash, std::equal_to<>>
        targets;
    targets.reserve(m_impl->buffers.size());

    for (const auto &[id, buffer] : m_impl->buffers) {
        const std::string &path =
            buffer.path;
        if (!pathAtOrBelow(path, oldRoot)) {
            continue;
        }
        std::string target = newRoot;
        target.append(
            path,
            oldRoot.size(),
            std::string::npos);
        if (!targets.emplace(target).second) {
            return false;
        }
        affected.insert(id);
        changes.push_back(
            PathChange{id, path, std::move(target)});
    }

    for (const PathChange &change : changes) {
        const auto owner =
            m_impl->paths.find(change.newPath);
        if (owner != m_impl->paths.end()
            && !affected.contains(owner->second)) {
            if (!replaceConflictingBuffers) {
                return false;
            }
            displaced.insert(owner->second);
        }
    }

    if (replaceConflictingBuffers) {
        if (std::ranges::any_of(
                displaced,
                [this](const BufferId id) {
                    return m_impl->bufferAttached(id);
                })) {
            return false;
        }
        (void)m_impl->eraseBuffers(displaced);
    }

    for (const PathChange &change : changes) {
        m_impl->paths.erase(change.oldPath);
    }
    for (PathChange &change : changes) {
        m_impl->paths.emplace(change.newPath, change.id);
        m_impl->buffers.at(change.id)
            .path = std::move(change.newPath);
    }
    if (rebound != nullptr) {
        *rebound = changes.size();
    }
    vkui::writeDiagnostic(
        vkui::DiagnosticLevel::Info,
        QStringLiteral("vkcore.buffer"),
        QStringLiteral("paths.rebound"),
        QStringLiteral("Buffer subtree paths updated"),
        QJsonObject{
            {QStringLiteral("old_root"),
             QString::fromStdString(oldRoot)},
            {QStringLiteral("new_root"),
             QString::fromStdString(newRoot)},
            {QStringLiteral("count"),
             static_cast<qint64>(changes.size())},
        });
    return true;
}

bool VkCore::removeBuffer(const BufferId id)
{
    if (!m_impl->buffers.contains(id)) {
        return false;
    }
    const bool removed = m_impl->eraseBuffers(
               std::unordered_set<BufferId>{id})
        == 1;
    if (removed) {
        vkui::writeDiagnostic(
            vkui::DiagnosticLevel::Info,
            QStringLiteral("vkcore.buffer"),
            QStringLiteral("remove"),
            QStringLiteral("Buffer removed"),
            QJsonObject{
                {QStringLiteral("buffer"), static_cast<qint64>(id)},
            });
    }
    return removed;
}

std::size_t VkCore::removeBuffersUnderPath(
    const std::string_view rawRoot)
{
    const std::string_view root =
        normalizedPathRoot(rawRoot);
    if (root.empty()) {
        return 0;
    }
    std::unordered_set<BufferId> removed;
    removed.reserve(m_impl->buffers.size());
    for (const auto &[id, buffer] : m_impl->buffers) {
        if (pathAtOrBelow(
                buffer.path, root)) {
            removed.insert(id);
        }
    }
    return m_impl->eraseBuffers(removed);
}

bool VkCore::detachViewBuffer(const ViewId id)
{
    const auto found = m_impl->views.find(id);
    if (found == m_impl->views.end()) {
        return false;
    }
    if (found->second.buffer == 0) {
        return true;
    }
    if (m_impl->insertUndoBuffer
        && *m_impl->insertUndoBuffer
            == found->second.buffer) {
        m_impl->closeInsertUndoBlock();
    }
    const BufferId detached = found->second.buffer;
    found->second.buffer = 0;
    found->second.changeIndex = 0;
    found->second.preferredColumn.reset();
    found->second.preferredDisplayRowColumn.reset();
    if (m_impl->pendingView == id
        || (m_impl->pendingDisplayMotion
            && m_impl->pendingDisplayMotion->window == id)
        || m_impl->inputContextView == id) {
        cancelPendingInput();
    }
    vkui::writeDiagnostic(
        vkui::DiagnosticLevel::Debug,
        QStringLiteral("vkcore.buffer"),
        QStringLiteral("detach"),
        QStringLiteral("Buffer detached from view"),
        QJsonObject{
            {QStringLiteral("view"), static_cast<qint64>(id)},
            {QStringLiteral("buffer"), static_cast<qint64>(detached)},
        });
    return true;
}

std::optional<BufferSnapshot> VkCore::buffer(
    const BufferId id) const
{
    const auto found = m_impl->buffers.find(id);
    return found == m_impl->buffers.end()
        ? std::nullopt
        : found->second.snapshot();
}

std::optional<vkui::buffer::Descriptor>
VkCore::bufferDataDescriptor(const BufferId id) const
{
    const auto found = m_impl->buffers.find(id);
    return found == m_impl->buffers.end()
        ? std::nullopt
        : found->second.storage.describe();
}

vkui::buffer::RangeRead VkCore::readBufferDataRange(
    const BufferId id,
    vkui::buffer::RangeRequest request) const noexcept
{
    const auto found = m_impl->buffers.find(id);
    return found == m_impl->buffers.end()
        ? vkui::buffer::RangeRead{}
        : found->second.storage.read(std::move(request));
}

bool VkCore::requestBufferPrefetch(
    const BufferId id,
    vkui::buffer::PrefetchRequest request,
    const std::stop_token stopToken,
    vkui::buffer::PrefetchCompletion completion) const noexcept
{
    const auto found = m_impl->buffers.find(id);
    return found != m_impl->buffers.end()
        && found->second.storage.requestPrefetch(
            request,
            stopToken,
            std::move(completion));
}

std::optional<BufferRangeSnapshot> VkCore::readBufferRange(
    const BufferId id,
    const std::size_t requestedOffset,
    const std::size_t maxLength) const
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.end()) {
        return std::nullopt;
    }
    const auto descriptor = found->second.storage.describe();
    if (!descriptor) {
        return std::nullopt;
    }
    return readBufferRangeAtRevision(
        id,
        requestedOffset,
        maxLength,
        descriptor->revision);
}

std::optional<BufferRangeSnapshot>
VkCore::readBufferRangeAtRevision(
    const BufferId id,
    const std::size_t requestedOffset,
    const std::size_t maxLength,
    const vkui::buffer::Revision expectedRevision) const
{
    const auto found = m_impl->buffers.find(id);
    if (found == m_impl->buffers.end()) {
        return std::nullopt;
    }
    const auto descriptor = found->second.storage.describe();
    if (!descriptor
        || descriptor->revision != expectedRevision) {
        return std::nullopt;
    }
    const std::size_t offset = std::min(
        requestedOffset, descriptor->size);
    const auto range = found->second.storage.read(
        vkui::buffer::RangeRequest{
            offset,
            maxLength,
            expectedRevision});
    if (!range.ok()) {
        return std::nullopt;
    }
    return BufferRangeSnapshot{
        id,
        range.revision,
        offset,
        range.totalSize,
        std::move(range.text)};
}

std::optional<std::string> VkCore::bufferPath(
    const BufferId id) const
{
    const auto found = m_impl->buffers.find(id);
    return found == m_impl->buffers.end()
        ? std::nullopt
        : std::optional<std::string>(
              found->second.path);
}

std::optional<BufferSnapshot> VkCore::activeBuffer(
    const ViewId view) const
{
    const auto found = m_impl->views.find(view);
    return found == m_impl->views.end()
        ? std::nullopt
        : buffer(found->second.buffer);
}

std::size_t VkCore::bufferCount() const noexcept
{
    return m_impl->buffers.size();
}

void VkCore::clearWorkspace()
{
    m_impl->activeInsertRepeat.reset();
    m_impl->activeInsertRepeatValid = false;
    m_impl->promptInput.reset();
    std::unordered_set<BufferId> removed;
    removed.reserve(m_impl->buffers.size());
    for (const auto &[id, buffer] : m_impl->buffers) {
        static_cast<void>(buffer);
        removed.insert(id);
    }
    static_cast<void>(m_impl->eraseBuffers(removed));
    // An empty workspace can still have a pending navigation-surface
    // grammar. A vault switch is always an input transaction boundary.
    cancelPendingInput();
}

} // namespace vkui::vk
