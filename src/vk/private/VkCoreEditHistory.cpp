#include "VkCoreInternal.h"

namespace vkui::vk {

void VkCore::Implementation::resetUndoHistory(
    Buffer &buffer,
    const BufferId bufferId)
{
    if (buffer.storage.isExternalSession()) {
        (void)buffer.storage.resetExternalHistory();
        buffer.undo.reset();
    } else {
        buffer.undo.emplace();
    }
    buffer.lastOperationUndoNode.reset();
    buffer.lastExternalMetadataGroup.reset();
    buffer.changeList.clear();
    for (auto &[viewId, view] : views) {
        static_cast<void>(viewId);
        if (view.buffer == bufferId) {
            view.changeIndex = 0;
        }
    }
    if (insertUndoBuffer == bufferId) {
        externalEditGroups.erase(bufferId);
        insertUndoBuffer.reset();
    }
}

void VkCore::Implementation::recordChangePosition(
    Buffer &buffer,
    const BufferId bufferId,
    const std::size_t rawOffset,
    const bool startedUndoNode,
    const ViewId initiatingView)
{
    const std::size_t changeOffset = std::min(
        rawOffset, buffer.text().size());
    bool append = buffer.changeList.empty();
    if (!append && startedUndoNode) {
        const Cursor previous = cursorAtOffset(
            buffer, buffer.changeList.back(), true);
        const Cursor current = cursorAtOffset(
            buffer, changeOffset, true);
        const std::size_t columnDistance =
            previous.column > current.column
            ? previous.column - current.column
            : current.column - previous.column;
        // Neovim coalesces nearby changes on one line even when a new
        // undo block began. Its fallback distance is 79 columns.
        append = previous.line != current.line
            || columnDistance > 79;
    }

    if (!append) {
        buffer.changeList.back() = changeOffset;
        const auto initiator = views.find(initiatingView);
        if (initiator != views.end()
            && initiator->second.buffer == bufferId) {
            initiator->second.changeIndex =
                buffer.changeList.size();
        }
        return;
    }

    const std::size_t oldLength = buffer.changeList.size();
    std::vector<ViewId> liveTailViews;
    for (const auto &[viewId, view] : views) {
        if (view.buffer == bufferId
            && view.changeIndex >= oldLength) {
            liveTailViews.push_back(viewId);
        }
    }

    constexpr std::size_t MaximumChangeEntries = 100;
    if (buffer.changeList.size() >= MaximumChangeEntries) {
        buffer.changeList.erase(buffer.changeList.begin());
        for (auto &[viewId, view] : views) {
            static_cast<void>(viewId);
            if (view.buffer == bufferId
                && view.changeIndex > 0) {
                --view.changeIndex;
            }
        }
    }
    buffer.changeList.push_back(changeOffset);
    for (const ViewId viewId : liveTailViews) {
        const auto found = views.find(viewId);
        if (found != views.end()
            && found->second.buffer == bufferId) {
            found->second.changeIndex =
                buffer.changeList.size();
        }
    }
    const auto initiator = views.find(initiatingView);
    if (initiator != views.end()
        && initiator->second.buffer == bufferId) {
        initiator->second.changeIndex =
            buffer.changeList.size();
    }
}

[[nodiscard]] std::size_t VkCore::Implementation::ensureUndoNode(
    Buffer &buffer,
    const bool groupWithInsert,
    std::vector<WindowCursorState> beforeCursors)
{
    UndoHistory& history = *buffer.undo;
    if (groupWithInsert
        && history.openInsertNode
        && history.current == *history.openInsertNode) {
        return *history.openInsertNode;
    }

    UndoNode node;
    node.parent = history.current;
    node.beforeCursors = std::move(beforeCursors);
    const std::size_t index = history.nodes.size();
    history.nodes.push_back(std::move(node));
    UndoNode &parent = history.nodes[history.current];
    parent.children.push_back(index);
    parent.preferredChild = index;
    history.current = index;
    if (groupWithInsert) {
        history.openInsertNode = index;
    }
    return index;
}

void VkCore::Implementation::patchLineStarts(
    Buffer &buffer,
    const std::size_t start,
    const std::size_t end,
    const std::u16string_view inserted)
{
    if (buffer.lineStarts.external()) {
        return;
    }
    const auto firstRemoved = std::upper_bound(
        buffer.lineStarts.cbegin(),
        buffer.lineStarts.cend(),
        start);
    const auto afterRemoved = std::upper_bound(
        firstRemoved,
        buffer.lineStarts.cend(),
        end);
    const std::size_t insertionIndex =
        static_cast<std::size_t>(
            std::distance(
                buffer.lineStarts.cbegin(),
                firstRemoved));
    buffer.lineStarts.erase(
        buffer.lineStarts.cbegin()
            + static_cast<std::ptrdiff_t>(
                insertionIndex),
        afterRemoved);

    const std::size_t removed = end - start;
    buffer.lineStarts.shiftOwnedFrom(insertionIndex, static_cast<std::ptrdiff_t>(inserted.size()) -
                                                         static_cast<std::ptrdiff_t>(removed));

    std::vector<std::size_t> addedStarts;
    addedStarts.reserve(
        static_cast<std::size_t>(
            std::count(
                inserted.cbegin(),
                inserted.cend(),
                u'\n')));
    for (std::size_t index = 0;
         index < inserted.size();
         ++index) {
        if (inserted[index] == u'\n') {
            addedStarts.push_back(
                start + index + 1);
        }
    }
    buffer.lineStarts.insert(
        buffer.lineStarts.cbegin()
            + static_cast<std::ptrdiff_t>(
                insertionIndex),
        addedStarts.cbegin(),
        addedStarts.cend());
}

void VkCore::Implementation::desynchronizeExternalAuthority(DispatchResult* const result,
                                                            Buffer& buffer, const BufferId bufferId,
                                                            const ViewId viewId,
                                                            const std::uint64_t revision,
                                                            const std::size_t size) {
    buffer.authorityDesynchronized = true;
    buffer.desynchronizedRevision = revision;
    buffer.desynchronizedSize = size;
    buffer.displayLines.clear();
    buffer.storage.invalidateExternalProjection();
    externalEditGroups.erase(bufferId);
    if (result == nullptr) {
        return;
    }
    Event event;
    event.type = EventType::ExternalAuthorityDesynchronized;
    event.view = viewId;
    event.buffer = bufferId;
    event.authorityRevision = revision;
    event.authoritySize = size;
    event.message = "external authority committed without a usable immutable snapshot";
    result->events.push_back(std::move(event));
}

[[nodiscard]] bool VkCore::Implementation::mutateBuffer(
    DispatchResult *const result,
    const BufferId bufferId,
    const std::size_t start,
    const std::size_t end,
    std::u16string inserted,
    const std::optional<std::pair<ViewId, Cursor>>
        authoritativeCursor,
    const bool recordUndo,
    const bool emitCursorEvents,
    const std::optional<AuthoritativeSelectionOffsetState>
        authoritativeSelectionOffsets)
{
    const auto foundBuffer = buffers.find(bufferId);
    if (foundBuffer == buffers.end()) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    if (buffer.storage.externalReadFaulted() && !buffer.authorityDesynchronized) {
        const auto descriptor = buffer.storage.lastExternalDescriptor();
        desynchronizeExternalAuthority(result, buffer, bufferId, activeView,
                                       descriptor ? descriptor->revision : 0,
                                       descriptor ? descriptor->size : 0);
    }
    if (buffer.authorityDesynchronized) {
        return false;
    }
    const std::size_t boundedStart =
        std::min(start, buffer.text().size());
    const std::size_t boundedEnd =
        std::clamp(
            end,
            boundedStart,
            buffer.text().size());
    const std::size_t removed =
        boundedEnd - boundedStart;
    const auto adjustAnchor = [boundedStart, boundedEnd, removed,
                               insertedSize = inserted.size()](const std::size_t oldOffset) {
        if (oldOffset > boundedEnd) {
            return oldOffset - removed + insertedSize;
        }
        if (oldOffset >= boundedStart) {
            return boundedStart + std::min(oldOffset - boundedStart, insertedSize);
        }
        return oldOffset;
    };
    if (removed == inserted.size()
        && std::equal(
            inserted.cbegin(),
            inserted.cend(),
            buffer.text().cbegin()
                + static_cast<std::ptrdiff_t>(
                    boundedStart))) {
        return true;
    }
    if (buffer.readOnly) {
        if (result != nullptr) {
            Event error;
            error.type = EventType::InputError;
            error.view = activeView;
            error.buffer = bufferId;
            error.message = "buffer is read-only";
            result->events.push_back(
                std::move(error));
        }
        return false;
    }

    struct ViewOffsetState final
    {
        ViewId id = 0;
        std::size_t cursor = 0;
        std::optional<std::size_t> selectionAnchor;
    };
    std::vector<ViewOffsetState> viewOffsets;
    viewOffsets.reserve(views.size());
    for (const auto &[id, view] : views) {
        if (view.buffer == bufferId) {
            const auto cursor =
                view.cursors.find(bufferId);
            viewOffsets.push_back(
                ViewOffsetState{
                    id,
                    cursor == view.cursors.cend()
                        ? 0
                        : offset(buffer, cursor->second),
                    view.selectionAnchorOffset});
        }
    }

    const bool externalSession = buffer.storage.isExternalSession();
    std::optional<std::uint64_t> externalMetadataGroup;
    bool startedExternalMetadataGroup = false;
    if (externalSession) {
        vkui::buffer::EditRequest request;
        request.offset = boundedStart;
        request.removedLength = removed;
        request.inserted = inserted;
        request.expectedRevision = buffer.revision();
        const ViewId selectionView = authoritativeSelectionOffsets
                                         ? authoritativeSelectionOffsets->view
                                     : authoritativeCursor ? authoritativeCursor->first
                                                           : activeView;
        const auto selected =
            std::ranges::find_if(viewOffsets, [selectionView](const ViewOffsetState& state) {
                return state.id == selectionView;
            });
        if (selected != viewOffsets.cend()) {
            request.selectionBefore = vkui::buffer::TextSelection{
                selected->selectionAnchor.value_or(selected->cursor), selected->cursor};
            request.selectionAfter =
                authoritativeSelectionOffsets
                    ? std::optional<vkui::buffer::TextSelection>(
                          vkui::buffer::TextSelection{authoritativeSelectionOffsets->anchor,
                                                      authoritativeSelectionOffsets->cursor})
                    : std::optional<vkui::buffer::TextSelection>(vkui::buffer::TextSelection{
                          adjustAnchor(selected->selectionAnchor.value_or(selected->cursor)),
                          adjustAnchor(selected->cursor)});
        }
        if (const auto group = externalEditGroups.find(bufferId);
            group != externalEditGroups.end()) {
            request.group = group->second;
        }
        externalMetadataGroup = request.group;
        startedExternalMetadataGroup =
            !externalMetadataGroup || buffer.lastExternalMetadataGroup != externalMetadataGroup;
        const auto editResult = buffer.storage.edit(std::move(request));
        if (editResult.status == vkui::buffer::EditStatus::CommittedSnapshotUnavailable) {
            desynchronizeExternalAuthority(result, buffer, bufferId, activeView,
                                           editResult.revision, editResult.size);
            return false;
        }
        if (!editResult.accepted()) {
            if (result != nullptr) {
                Event error;
                error.type = EventType::InputError;
                error.view = activeView;
                error.buffer = bufferId;
                error.message = editResult.status == vkui::buffer::EditStatus::StaleRevision
                                    ? "external document revision is stale"
                                    : "external document rejected edit";
                result->events.push_back(std::move(error));
            }
            return false;
        }
        if (editResult.status == vkui::buffer::EditStatus::Unchanged) {
            return true;
        }
    }

    std::optional<std::size_t> undoNode;
    bool startedUndoNode = false;
    std::u16string removedText;
    if (recordUndo && !buffer.storage.isExternalSession()) {
        removedText = buffer.text().substr(boundedStart, removed);
        const bool groupWithInsert = insertUndoBuffer && *insertUndoBuffer == bufferId;
        const std::size_t nodeCount = buffer.undo->nodes.size();
        undoNode = ensureUndoNode(buffer, groupWithInsert, captureWindowCursors(bufferId));
        startedUndoNode = buffer.undo->nodes.size() != nodeCount;
    }

    for (auto &mark : buffer.localMarks) {
        if (mark) {
            *mark = adjustAnchor(*mark);
        }
    }
    const auto adjustLocalMark = [&adjustAnchor](
                                     auto &mark) {
        if (mark) {
            *mark = adjustAnchor(*mark);
        }
    };
    adjustLocalMark(buffer.lastChangeMark);
    adjustLocalMark(buffer.lastInsertExitMark);
    adjustLocalMark(buffer.lastOperationStartMark);
    adjustLocalMark(buffer.lastOperationEndMark);
    adjustLocalMark(buffer.lastVisualStartMark);
    adjustLocalMark(buffer.lastVisualEndMark);
    adjustLocalMark(buffer.lastCursorMark);
    for (std::size_t &change : buffer.changeList) {
        change = adjustAnchor(change);
    }
    for (auto &mark : globalMarks) {
        if (mark && mark->buffer == bufferId) {
            mark->offset = adjustAnchor(
                mark->offset);
        }
    }
    for (auto &[viewId, view] : views) {
        static_cast<void>(viewId);
        for (Location &jump : view.jumpList) {
            if (jump.buffer == bufferId) {
                jump.offset = adjustAnchor(jump.offset);
            }
        }
        if (view.previousContext
            && view.previousContext->buffer == bufferId) {
            view.previousContext->offset = adjustAnchor(
                view.previousContext->offset);
        }
    }
    if (pendingLocationMove) {
        if (pendingLocationMove->origin.buffer == bufferId) {
            pendingLocationMove->origin.offset =
                adjustAnchor(
                    pendingLocationMove->origin.offset);
        }
        if (pendingLocationMove->destination.buffer
            == bufferId) {
            pendingLocationMove->destination.offset =
                adjustAnchor(
                    pendingLocationMove->destination.offset);
        }
    }

    patchLineStarts(
        buffer,
        boundedStart,
        boundedEnd,
        inserted);
    if (!externalSession && !buffer.editText(boundedStart, removed, inserted)) {
        return false;
    }
    buffer.displayLines.clear();
    if (recordUndo) {
        buffer.lastChangeMark = boundedStart;
        recordChangePosition(buffer, bufferId, boundedStart,
                             externalSession ? startedExternalMetadataGroup : startedUndoNode,
                             authoritativeCursor             ? authoritativeCursor->first
                             : authoritativeSelectionOffsets ? authoritativeSelectionOffsets->view
                                                             : activeView);
        const std::size_t operationEnd = inserted.empty()
            ? boundedStart
            : boundedStart + inserted.size() - 1;
        const bool continuesOperation = externalSession
                                            ? externalMetadataGroup && !startedExternalMetadataGroup
                                            : undoNode && buffer.lastOperationUndoNode == undoNode;
        if (continuesOperation) {
            buffer.lastOperationStartMark = std::min(
                buffer.lastOperationStartMark.value_or(
                    boundedStart),
                boundedStart);
            buffer.lastOperationEndMark = std::max(
                buffer.lastOperationEndMark.value_or(
                    operationEnd),
                operationEnd);
        } else {
            buffer.lastOperationStartMark = boundedStart;
            buffer.lastOperationEndMark = operationEnd;
            buffer.lastOperationUndoNode = undoNode;
        }
        if (externalSession) {
            buffer.lastExternalMetadataGroup = externalMetadataGroup;
            buffer.lastOperationUndoNode.reset();
        }
    }

    for (const ViewOffsetState &oldState : viewOffsets) {
        View &view = views.at(oldState.id);
        std::size_t adjusted = oldState.cursor;
        if (oldState.cursor >= boundedEnd) {
            adjusted =
                oldState.cursor - removed + inserted.size();
        } else if (oldState.cursor > boundedStart) {
            adjusted =
                boundedStart + inserted.size();
        }
        view.cursors[bufferId] =
            cursorAtOffset(
                buffer,
                adjusted,
                !enabled
                    || baseMode == Mode::Insert
                    || baseMode == Mode::Replace);
        // Display layout is presentation-derived state. Do not scan a
        // potentially multi-megabyte edited line on every host keystroke;
        // the first display-aware motion/snapshot rebuilds it lazily.
        view.displayColumns.erase(bufferId);
        view.preferredColumn.reset();
        view.preferredDisplayRowColumn.reset();
        view.selectionAnchorOffset = oldState.selectionAnchor
            ? std::optional<std::size_t>(adjustAnchor(
                  *oldState.selectionAnchor))
            : std::nullopt;
    }

    if (authoritativeSelectionOffsets) {
        const auto foundView = views.find(
            authoritativeSelectionOffsets->view);
        if (foundView != views.end()
            && foundView->second.buffer == bufferId) {
            const std::size_t cursorOffset = std::min(
                authoritativeSelectionOffsets->cursor,
                buffer.text().size());
            const std::size_t anchorOffset = std::min(
                authoritativeSelectionOffsets->anchor,
                buffer.text().size());
            foundView->second.cursors[bufferId] = cursorAtOffset(
                buffer,
                cursorOffset,
                !enabled
                    || baseMode == Mode::Insert
                    || baseMode == Mode::Replace);
            foundView->second.selectionAnchorOffset =
                anchorOffset == cursorOffset
                ? std::nullopt
                : std::optional<std::size_t>(anchorOffset);
            foundView->second.displayColumns.erase(bufferId);
            foundView->second.preferredColumn.reset();
            foundView->second.preferredDisplayRowColumn.reset();
        }
    } else if (authoritativeCursor) {
        const auto foundView =
            views.find(authoritativeCursor->first);
        if (foundView != views.end()
            && foundView->second.buffer == bufferId) {
            Cursor cursor =
                authoritativeCursor->second;
            cursor = clampCursor(
                buffer,
                cursor,
                !enabled
                    || baseMode == Mode::Insert
                    || baseMode == Mode::Replace);
            foundView->second.cursors[bufferId] =
                cursor;
            foundView->second.selectionAnchorOffset.reset();
            foundView->second.displayColumns.erase(
                bufferId);
            foundView->second.preferredColumn.reset();
            foundView->second
                .preferredDisplayRowColumn.reset();
        }
    }

    if (undoNode) {
        UndoNode& node = buffer.undo->nodes[*undoNode];
        node.deltas.push_back(
            EditDelta{
                boundedStart,
                std::move(removedText),
                inserted});
        node.afterCursors =
            captureWindowCursors(bufferId);
    }

    if (result == nullptr) {
        return true;
    }
    Event edit;
    edit.type = EventType::BufferEdited;
    edit.view = activeView;
    edit.buffer = bufferId;
    edit.editOffset = boundedStart;
    edit.editRemoved = removed;
    edit.editInserted = std::move(inserted);
    result->events.push_back(std::move(edit));
    if (emitCursorEvents) {
        for (const ViewOffsetState &oldState : viewOffsets) {
            emitCursor(*result, oldState.id, views.at(oldState.id));
        }
    }
    return true;
}

[[nodiscard]] bool VkCore::Implementation::mutateExternalBatch(
    DispatchResult* const result, const BufferId bufferId,
    std::vector<vkui::buffer::TransactionEdit> edits,
    const vkui::buffer::TextSelection selectionBefore,
    const vkui::buffer::TextSelection selectionAfter, const ViewId authoritativeView,
    const std::optional<std::uint64_t> group, const bool emitCursorEvents) {
    const auto foundBuffer = buffers.find(bufferId);
    if (foundBuffer == buffers.end() || !foundBuffer->second.storage.isExternalSession() ||
        foundBuffer->second.readOnly) {
        return false;
    }
    Buffer& buffer = foundBuffer->second;
    if (buffer.storage.externalReadFaulted() && !buffer.authorityDesynchronized) {
        const auto descriptor = buffer.storage.lastExternalDescriptor();
        desynchronizeExternalAuthority(result, buffer, bufferId, authoritativeView,
                                       descriptor ? descriptor->revision : 0,
                                       descriptor ? descriptor->size : 0);
    }
    if (buffer.authorityDesynchronized) {
        return false;
    }
    const auto descriptor = buffer.storage.describe();
    if (!descriptor || selectionBefore.anchor > descriptor->size ||
        selectionBefore.cursor > descriptor->size) {
        return false;
    }
    std::size_t candidateSize = descriptor->size;
    for (const auto& edit : edits) {
        if (edit.offset > candidateSize || edit.removedLength > candidateSize - edit.offset ||
            edit.inserted.size() >
                std::numeric_limits<std::size_t>::max() - (candidateSize - edit.removedLength)) {
            return false;
        }
        candidateSize = candidateSize - edit.removedLength + edit.inserted.size();
    }
    if (selectionAfter.anchor > candidateSize || selectionAfter.cursor > candidateSize) {
        return false;
    }

    struct ViewOffsets final {
        ViewId id = 0;
        std::size_t cursor = 0;
        std::optional<std::size_t> anchor;
    };
    std::vector<ViewOffsets> viewOffsets;
    for (const auto& [id, view] : views) {
        if (view.buffer != bufferId) {
            continue;
        }
        const auto cursor = view.cursors.find(bufferId);
        viewOffsets.push_back(
            ViewOffsets{id, cursor == view.cursors.cend() ? 0 : offset(buffer, cursor->second),
                        view.selectionAnchorOffset});
    }

    vkui::buffer::EditTransactionRequest transaction;
    transaction.edits = edits;
    transaction.expectedRevision = descriptor->revision;
    transaction.selectionBefore = selectionBefore;
    transaction.selectionAfter = selectionAfter;
    transaction.group = group;
    const bool startedExternalMetadataGroup = !group || buffer.lastExternalMetadataGroup != group;
    const auto applied = buffer.storage.editTransaction(std::move(transaction));
    if (applied.status == vkui::buffer::EditStatus::CommittedSnapshotUnavailable) {
        desynchronizeExternalAuthority(result, buffer, bufferId, authoritativeView,
                                       applied.revision, applied.size);
        return false;
    }
    if (!applied.accepted()) {
        return false;
    }

    const auto adjustOffset = [&edits](std::size_t value) {
        for (const auto& edit : edits) {
            const std::size_t end = edit.offset + edit.removedLength;
            if (value > end) {
                value = value - edit.removedLength + edit.inserted.size();
            } else if (value >= edit.offset) {
                value = edit.offset + std::min(value - edit.offset, edit.inserted.size());
            }
        }
        return value;
    };
    for (auto& mark : buffer.localMarks) {
        if (mark) {
            *mark = adjustOffset(*mark);
        }
    }
    const auto adjustMark = [&adjustOffset](auto& mark) {
        if (mark) {
            *mark = adjustOffset(*mark);
        }
    };
    adjustMark(buffer.lastChangeMark);
    adjustMark(buffer.lastInsertExitMark);
    adjustMark(buffer.lastOperationStartMark);
    adjustMark(buffer.lastOperationEndMark);
    adjustMark(buffer.lastVisualStartMark);
    adjustMark(buffer.lastVisualEndMark);
    adjustMark(buffer.lastCursorMark);
    for (std::size_t& change : buffer.changeList) {
        change = adjustOffset(change);
    }
    for (auto& mark : globalMarks) {
        if (mark && mark->buffer == bufferId) {
            mark->offset = adjustOffset(mark->offset);
        }
    }
    for (auto& [id, view] : views) {
        static_cast<void>(id);
        for (Location& jump : view.jumpList) {
            if (jump.buffer == bufferId) {
                jump.offset = adjustOffset(jump.offset);
            }
        }
        if (view.previousContext && view.previousContext->buffer == bufferId) {
            view.previousContext->offset = adjustOffset(view.previousContext->offset);
        }
    }
    if (pendingLocationMove) {
        if (pendingLocationMove->origin.buffer == bufferId) {
            pendingLocationMove->origin.offset = adjustOffset(pendingLocationMove->origin.offset);
        }
        if (pendingLocationMove->destination.buffer == bufferId) {
            pendingLocationMove->destination.offset =
                adjustOffset(pendingLocationMove->destination.offset);
        }
    }
    for (const ViewOffsets& old : viewOffsets) {
        View& view = views.at(old.id);
        const std::size_t cursor =
            old.id == authoritativeView ? selectionAfter.cursor : adjustOffset(old.cursor);
        view.cursors[bufferId] = cursorAtOffset(buffer, cursor, true);
        const std::size_t anchor = old.id == authoritativeView
                                       ? selectionAfter.anchor
                                       : adjustOffset(old.anchor.value_or(old.cursor));
        view.selectionAnchorOffset =
            anchor == cursor ? std::nullopt : std::optional<std::size_t>(anchor);
        view.topline = std::min(view.topline, buffer.lineStarts.size() - 1);
        view.displayColumns[bufferId] = displayColumnForBufferColumn(
            buffer, view.cursors[bufferId].line, view.cursors[bufferId].column);
        view.preferredColumn.reset();
        view.preferredDisplayRowColumn.reset();
    }
    buffer.displayLines.clear();

    if (!edits.empty()) {
        const auto adjustOne = [](std::size_t value, const vkui::buffer::TransactionEdit& edit) {
            const std::size_t end = edit.offset + edit.removedLength;
            if (value > end) {
                return value - edit.removedLength + edit.inserted.size();
            }
            if (value >= edit.offset) {
                return edit.offset + std::min(value - edit.offset, edit.inserted.size());
            }
            return value;
        };
        std::optional<std::size_t> operationStart;
        std::optional<std::size_t> operationEnd;
        for (const auto& edit : edits) {
            if (operationStart) {
                *operationStart = adjustOne(*operationStart, edit);
                *operationEnd = adjustOne(*operationEnd, edit);
            }
            const std::size_t editEnd =
                edit.inserted.empty() ? edit.offset : edit.offset + edit.inserted.size() - 1;
            operationStart = std::min(operationStart.value_or(edit.offset), edit.offset);
            operationEnd = std::max(operationEnd.value_or(editEnd), editEnd);
        }

        buffer.lastChangeMark = edits.back().offset;
        recordChangePosition(buffer, bufferId, *buffer.lastChangeMark, startedExternalMetadataGroup,
                             authoritativeView);
        if (group && !startedExternalMetadataGroup) {
            buffer.lastOperationStartMark =
                std::min(buffer.lastOperationStartMark.value_or(*operationStart), *operationStart);
            buffer.lastOperationEndMark =
                std::max(buffer.lastOperationEndMark.value_or(*operationEnd), *operationEnd);
        } else {
            buffer.lastOperationStartMark = operationStart;
            buffer.lastOperationEndMark = operationEnd;
        }
        buffer.lastExternalMetadataGroup = group;
        buffer.lastOperationUndoNode.reset();
    }

    if (result != nullptr) {
        for (const auto& committed : edits) {
            Event edit;
            edit.type = EventType::BufferEdited;
            edit.view = authoritativeView;
            edit.buffer = bufferId;
            edit.editOffset = committed.offset;
            edit.editRemoved = committed.removedLength;
            edit.editInserted = committed.inserted;
            result->events.push_back(std::move(edit));
        }
        if (emitCursorEvents) {
            for (const ViewOffsets& old : viewOffsets) {
                emitCursor(*result, old.id, views.at(old.id));
            }
        }
    }
    return true;
}

[[nodiscard]] bool VkCore::Implementation::applyBufferEdit(
    DispatchResult &result,
    const BufferId bufferId,
    const std::size_t start,
    const std::size_t end,
    std::u16string inserted)
{
    return mutateBuffer(
        &result,
        bufferId,
        start,
        end,
        std::move(inserted));
}

void VkCore::Implementation::rememberChange(RepeatChange change)
{
    if (!replayingChange) {
        lastChange = std::move(change);
    }
}

void VkCore::Implementation::beginInsertRepeat(
    RepeatChange change,
    const std::size_t baseOffset,
    const bool changedBeforeInsert)
{
    if (replayingChange) {
        return;
    }
    change.changedBeforeInsert =
        changedBeforeInsert;
    change.insertedText.clear();
    activeInsertRepeat = std::move(change);
    insertRepeatBaseOffset = baseOffset;
    activeInsertRepeatValid = true;
}

void VkCore::Implementation::recordExternalInsertEdit(
    const ViewId viewId,
    const std::size_t editOffset,
    const std::size_t removed,
    const std::u16string_view inserted)
{
    if (!activeInsertRepeat
        || !activeInsertRepeatValid
        || baseMode != Mode::Insert
        || viewId != activeView
        || editOffset < insertRepeatBaseOffset) {
        return;
    }
    std::u16string &captured =
        activeInsertRepeat->insertedText;
    const std::size_t relative =
        editOffset - insertRepeatBaseOffset;
    if (relative > captured.size()
        || removed > captured.size() - relative) {
        activeInsertRepeatValid = false;
        return;
    }
    captured.replace(
        relative, removed, inserted);
}

void VkCore::Implementation::finishInsertRepeat(
    DispatchResult *const result)
{
    if (result != nullptr
        && activeInsertRepeat
        && activeInsertRepeatValid
        && (activeInsertRepeat->command
                == Command::OpenBelow
            || activeInsertRepeat->command
                == Command::OpenAbove)
        && activeInsertRepeat->count > 1) {
        const auto foundView = views.find(activeView);
        if (foundView != views.end()
            && foundView->second.buffer != 0) {
            const BufferId bufferId =
                foundView->second.buffer;
            const auto foundBuffer = buffers.find(bufferId);
            if (foundBuffer != buffers.end()) {
                std::u16string repeated;
                const std::size_t copies =
                    activeInsertRepeat->count - 1;
                const std::size_t unit =
                    activeInsertRepeat->insertedText.size()
                    + 1;
                if (copies
                    <= std::numeric_limits<std::size_t>::max()
                        / unit) {
                    repeated.reserve(copies * unit);
                }
                for (std::size_t index = 0;
                     index < copies;
                     ++index) {
                    repeated.push_back(u'\n');
                    repeated +=
                        activeInsertRepeat->insertedText;
                }
                const std::size_t insertion = offset(
                    foundBuffer->second,
                    foundView->second.cursors[bufferId]);
                if (mutateBuffer(
                        result,
                        bufferId,
                        insertion,
                        insertion,
                        repeated)) {
                    const auto updated = buffers.find(bufferId);
                    if (updated != buffers.end()) {
                        foundView->second.cursors[bufferId] =
                            cursorAtOffset(
                                updated->second,
                                insertion + repeated.size(),
                                true);
                        foundView->second.preferredColumn.reset();
                        emitCursor(
                            *result,
                            activeView,
                            foundView->second);
                    }
                }
            }
        }
    }
    if (result != nullptr
        && activeInsertRepeat
        && activeInsertRepeatValid
        && !activeInsertRepeat->insertedText.empty()
        && (activeInsertRepeat->command
                == Command::EnterInsert
            || activeInsertRepeat->command
                == Command::AppendInsert
            || activeInsertRepeat->command
                == Command::InsertAtLineStart
            || activeInsertRepeat->command
                == Command::AppendAtLineEnd)
        && activeInsertRepeat->count > 1) {
        const auto foundView = views.find(activeView);
        if (foundView != views.end()
            && foundView->second.buffer != 0) {
            const BufferId bufferId =
                foundView->second.buffer;
            const auto foundBuffer = buffers.find(bufferId);
            if (foundBuffer != buffers.end()) {
                std::u16string repeated;
                const std::size_t copies =
                    activeInsertRepeat->count - 1;
                if (copies
                    <= std::numeric_limits<std::size_t>::max()
                        / activeInsertRepeat->insertedText.size()) {
                    repeated.reserve(
                        copies
                        * activeInsertRepeat->insertedText.size());
                }
                for (std::size_t index = 0;
                     index < copies;
                     ++index) {
                    repeated += activeInsertRepeat->insertedText;
                }
                const std::size_t insertion = offset(
                    foundBuffer->second,
                    foundView->second.cursors[bufferId]);
                (void)mutateBuffer(
                    result,
                    bufferId,
                    insertion,
                    insertion,
                    std::move(repeated));
            }
        }
    }
    if (result != nullptr
        && activeBlockInsert
        && activeInsertRepeat
        && activeInsertRepeatValid
        && !activeInsertRepeat->insertedText.empty()) {
        const BlockInsertState block =
            *activeBlockInsert;
        auto initial = buffers.find(block.buffer);
        if (initial != buffers.end() && initial->second.storage.isExternalSession()) {
            Buffer& buffer = initial->second;
            std::vector<vkui::buffer::TransactionEdit> edits;
            edits.reserve(block.lastLine - block.firstLine);
            for (std::size_t line = block.lastLine; line > block.firstLine; --line) {
                if (line >= buffer.lineStarts.size()) {
                    continue;
                }
                const auto insertion =
                    blockInsertionEdit(buffer, line, block.displayColumn, block.padShortLines);
                if (!insertion) {
                    continue;
                }
                std::u16string replacement = insertion->replacement;
                replacement.insert(insertion->insertionOffset, activeInsertRepeat->insertedText);
                const std::size_t lineStart = buffer.lineStarts[line];
                edits.push_back(vkui::buffer::TransactionEdit{
                    lineStart + insertion->bufferStart,
                    insertion->bufferEnd - insertion->bufferStart, std::move(replacement)});
            }
            const auto foundView = views.find(block.window);
            if (foundView != views.end() && foundView->second.buffer == block.buffer) {
                const std::size_t cursor = offset(buffer, foundView->second.cursors[block.buffer]);
                const vkui::buffer::TextSelection selection{
                    foundView->second.selectionAnchorOffset.value_or(cursor), cursor};
                const auto group = externalEditGroups.find(block.buffer);
                (void)mutateExternalBatch(result, block.buffer, std::move(edits), selection,
                                          selection, block.window,
                                          group == externalEditGroups.end()
                                              ? std::nullopt
                                              : std::optional<std::uint64_t>(group->second));
            }
        } else {
            // Apply from the bottom upwards. Every edit offset is therefore
            // valid for the document state produced by the preceding edit,
            // and the entire block remains one open undo transaction.
            for (std::size_t line = block.lastLine; line > block.firstLine; --line) {
                auto found = buffers.find(block.buffer);
                if (found == buffers.end() || line >= found->second.lineStarts.size()) {
                    continue;
                }
                const auto insertion = blockInsertionEdit(found->second, line, block.displayColumn,
                                                          block.padShortLines);
                if (!insertion) {
                    continue;
                }
                std::u16string replacement = insertion->replacement;
                replacement.insert(insertion->insertionOffset, activeInsertRepeat->insertedText);
                const std::size_t lineStart = found->second.lineStarts[line];
                (void)mutateBuffer(result, block.buffer, lineStart + insertion->bufferStart,
                                   lineStart + insertion->bufferEnd, std::move(replacement));
            }
        }
    }
    if (activeInsertRepeat
        && activeInsertRepeatValid
        && (activeInsertRepeat->changedBeforeInsert
            || !activeInsertRepeat->insertedText.empty())
        && !replayingChange) {
        lastChange = *activeInsertRepeat;
    }
    activeInsertRepeat.reset();
    activeBlockInsert.reset();
    activeInsertRepeatValid = false;
    insertRepeatBaseOffset = 0;
}

[[nodiscard]] bool VkCore::Implementation::replayUndoNode(
    DispatchResult &result,
    const BufferId bufferId,
    const std::size_t nodeIndex,
    const bool redo)
{
    const auto foundBuffer = buffers.find(bufferId);
    if (foundBuffer == buffers.end() || !foundBuffer->second.undo ||
        nodeIndex >= foundBuffer->second.undo->nodes.size()) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const UndoNode& node = buffer.undo->nodes[nodeIndex];
    if (redo) {
        for (const EditDelta &delta : node.deltas) {
            if (!mutateBuffer(
                    &result,
                    bufferId,
                    delta.offset,
                    delta.offset + delta.removed.size(),
                    delta.inserted,
                    std::nullopt,
                    false,
                    false)) {
                return false;
            }
        }
        restoreWindowCursors(
            bufferId, node.afterCursors);
        return true;
    }

    for (auto delta = node.deltas.crbegin();
         delta != node.deltas.crend();
         ++delta) {
        if (!mutateBuffer(
                &result,
                bufferId,
                delta->offset,
                delta->offset + delta->inserted.size(),
                delta->removed,
                std::nullopt,
                false,
                false)) {
            return false;
        }
    }
    restoreWindowCursors(
        bufferId, node.beforeCursors);
    return true;
}

void VkCore::Implementation::undoOrRedo(
    DispatchResult &result,
    const WindowId windowId,
    const std::size_t count,
    const bool redo)
{
    closeInsertUndoBlock();
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.buffer == 0) {
        return;
    }
    const BufferId bufferId =
        foundView->second.buffer;
    const auto foundBuffer = buffers.find(bufferId);
    if (foundBuffer == buffers.end()) {
        return;
    }
    Buffer &buffer = foundBuffer->second;
    if (buffer.storage.externalReadFaulted() && !buffer.authorityDesynchronized) {
        const auto descriptor = buffer.storage.lastExternalDescriptor();
        desynchronizeExternalAuthority(&result, buffer, bufferId, windowId,
                                       descriptor ? descriptor->revision : 0,
                                       descriptor ? descriptor->size : 0);
        return;
    }
    if (buffer.authorityDesynchronized) {
        Event event;
        event.type = EventType::ExternalAuthorityDesynchronized;
        event.view = windowId;
        event.buffer = bufferId;
        event.authorityRevision = buffer.desynchronizedRevision;
        event.authoritySize = buffer.desynchronizedSize;
        event.message = "external authority is desynchronized";
        result.events.push_back(std::move(event));
        return;
    }
    if (buffer.readOnly) {
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.buffer = bufferId;
        error.message = "buffer is read-only";
        result.events.push_back(std::move(error));
        return;
    }

    if (buffer.storage.isExternalSession()) {
        const auto beforeDescriptor = buffer.storage.describe();
        if (!beforeDescriptor) {
            return;
        }
        struct ViewOffsets final {
            ViewId id = 0;
            std::size_t cursor = 0;
            std::optional<std::size_t> anchor;
        };
        std::vector<ViewOffsets> beforeViews;
        beforeViews.reserve(views.size());
        for (const auto& [id, view] : views) {
            if (view.buffer != bufferId) {
                continue;
            }
            const auto cursor = view.cursors.find(bufferId);
            beforeViews.push_back(
                ViewOffsets{id, cursor == view.cursors.cend() ? 0 : offset(buffer, cursor->second),
                            view.selectionAnchorOffset});
        }
        const auto replay = buffer.storage.replayExternalHistory(count, redo);
        if (replay.status == vkui::buffer::HistoryReplayStatus::CommittedSnapshotUnavailable) {
            desynchronizeExternalAuthority(&result, buffer, bufferId, windowId, replay.revision,
                                           replay.size);
            return;
        }
        if (!replay.accepted() || replay.status == vkui::buffer::HistoryReplayStatus::Unchanged) {
            return;
        }
        std::size_t candidateSize = beforeDescriptor->size;
        for (const auto& committed : replay.edits) {
            if (committed.offset > candidateSize ||
                committed.removedLength > candidateSize - committed.offset ||
                committed.inserted.size() > std::numeric_limits<std::size_t>::max() -
                                                (candidateSize - committed.removedLength)) {
                desynchronizeExternalAuthority(&result, buffer, bufferId, windowId, replay.revision,
                                               replay.size);
                return;
            }
            candidateSize = candidateSize - committed.removedLength + committed.inserted.size();
        }
        if (candidateSize != replay.size) {
            desynchronizeExternalAuthority(&result, buffer, bufferId, windowId, replay.revision,
                                           replay.size);
            return;
        }
        if ((replay.cursor && *replay.cursor > replay.size) ||
            (replay.selectionAnchor && (!replay.cursor || *replay.selectionAnchor > replay.size))) {
            desynchronizeExternalAuthority(&result, buffer, bufferId, windowId, replay.revision,
                                           replay.size);
            return;
        }
        const auto adjustOffset = [&replay](std::size_t value) {
            for (const auto& committed : replay.edits) {
                const std::size_t end = committed.offset + committed.removedLength;
                if (value > end) {
                    value = value - committed.removedLength + committed.inserted.size();
                } else if (value >= committed.offset) {
                    value = committed.offset +
                            std::min(value - committed.offset, committed.inserted.size());
                }
            }
            return value;
        };
        for (auto& mark : buffer.localMarks) {
            if (mark) {
                *mark = adjustOffset(*mark);
            }
        }
        const auto adjustMark = [&adjustOffset](auto& mark) {
            if (mark) {
                *mark = adjustOffset(*mark);
            }
        };
        adjustMark(buffer.lastChangeMark);
        adjustMark(buffer.lastInsertExitMark);
        adjustMark(buffer.lastOperationStartMark);
        adjustMark(buffer.lastOperationEndMark);
        adjustMark(buffer.lastVisualStartMark);
        adjustMark(buffer.lastVisualEndMark);
        adjustMark(buffer.lastCursorMark);
        for (std::size_t& change : buffer.changeList) {
            change = adjustOffset(change);
        }
        for (auto& mark : globalMarks) {
            if (mark && mark->buffer == bufferId) {
                mark->offset = adjustOffset(mark->offset);
            }
        }
        for (auto& [id, view] : views) {
            static_cast<void>(id);
            for (Location& jump : view.jumpList) {
                if (jump.buffer == bufferId) {
                    jump.offset = adjustOffset(jump.offset);
                }
            }
            if (view.previousContext && view.previousContext->buffer == bufferId) {
                view.previousContext->offset = adjustOffset(view.previousContext->offset);
            }
        }
        if (pendingLocationMove) {
            if (pendingLocationMove->origin.buffer == bufferId) {
                pendingLocationMove->origin.offset =
                    adjustOffset(pendingLocationMove->origin.offset);
            }
            if (pendingLocationMove->destination.buffer == bufferId) {
                pendingLocationMove->destination.offset =
                    adjustOffset(pendingLocationMove->destination.offset);
            }
        }
        for (const auto& committed : replay.edits) {
            Event edit;
            edit.type = EventType::BufferEdited;
            edit.view = windowId;
            edit.buffer = bufferId;
            edit.editOffset = committed.offset;
            edit.editRemoved = committed.removedLength;
            edit.editInserted = committed.inserted;
            result.events.push_back(std::move(edit));
        }
        for (const ViewOffsets& before : beforeViews) {
            View& view = views.at(before.id);
            const std::size_t cursor = before.id == windowId && replay.cursor
                                           ? *replay.cursor
                                           : adjustOffset(before.cursor);
            const std::size_t anchor = before.id == windowId && replay.cursor
                                           ? replay.selectionAnchor.value_or(cursor)
                                           : adjustOffset(before.anchor.value_or(before.cursor));
            view.cursors[bufferId] = cursorAtOffset(buffer, cursor, true);
            view.selectionAnchorOffset =
                anchor == cursor ? std::nullopt : std::optional<std::size_t>(anchor);
            view.topline = std::min(view.topline, buffer.lineStarts.size() - 1);
            view.displayColumns[bufferId] = displayColumnForBufferColumn(
                buffer, view.cursors[bufferId].line, view.cursors[bufferId].column);
            view.preferredColumn.reset();
            view.preferredDisplayRowColumn.reset();
        }
        buffer.displayLines.clear();
        emitAttachedCursors(result, bufferId);
        return;
    }

    bool changedText = false;
    for (std::size_t step = 0;
         step < count;
         ++step) {
        UndoHistory& history = *buffer.undo;
        if (redo) {
            const UndoNode &current =
                history.nodes[history.current];
            if (current.preferredChild
                == UndoNode::noNode) {
                break;
            }
            const std::size_t next =
                current.preferredChild;
            if (!replayUndoNode(
                    result, bufferId, next, true)) {
                break;
            }
            history.current = next;
        } else {
            if (history.current == 0) {
                break;
            }
            const std::size_t current =
                history.current;
            const std::size_t parent =
                history.nodes[current].parent;
            if (!replayUndoNode(
                    result, bufferId, current, false)) {
                break;
            }
            history.current = parent;
        }
        changedText = true;
    }

    if (!changedText) {
        return;
    }
    for (auto &[id, view] : views) {
        if (view.buffer != bufferId) {
            continue;
        }
        const std::size_t previousTopline =
            view.topline;
        view.topline = std::min(
            view.topline,
            buffer.lineStarts.size() - 1);
        if (view.topline != previousTopline) {
            emitViewport(result, id, view);
        }
    }
    emitAttachedCursors(result, bufferId);
}

[[nodiscard]] DispatchResult VkCore::Implementation::replayPublicHistory(
    const WindowId window,
    const std::size_t count,
    const bool redo)
{
    DispatchResult result;
    const auto found = views.find(window);
    if (found == views.end()
        || found->second.kind != ViewKind::Editor
        || found->second.buffer == 0) {
        return result;
    }

    result.disposition = InputDisposition::Consumed;
    const Mode before = effectiveMode();
    clearPendingState();
    pendingOperator.reset();
    pendingArgument.reset();
    normalCount = 0;
    prefix = CommandPrefix::None;
    selectedRegister = U'"';
    if (before == Mode::Insert) {
        finishInsertRepeat(&result);
    } else if (before == Mode::Replace) {
        finishReplaceMode(
            &result, window, false);
    } else if (before == Mode::Visual) {
        leaveVisualMode();
        baseMode = Mode::Normal;
    }

    undoOrRedo(
        result,
        window,
        std::max<std::size_t>(1, count),
        redo);
    if (baseMode == Mode::Insert
        || baseMode == Mode::Replace) {
        continueInsertUndoBlock(window);
    }
    emitModeIfChanged(result, before);
    return result;
}

} // namespace vkui::vk
