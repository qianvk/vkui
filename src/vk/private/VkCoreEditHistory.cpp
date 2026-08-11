#include "VkCoreInternal.h"

namespace vkui::vk {

void VkCore::Implementation::resetUndoHistory(
    Buffer &buffer,
    const BufferId bufferId)
{
    buffer.undo = UndoHistory{};
    buffer.lastOperationUndoNode.reset();
    buffer.changeList.clear();
    for (auto &[viewId, view] : views) {
        static_cast<void>(viewId);
        if (view.buffer == bufferId) {
            view.changeIndex = 0;
        }
    }
    if (insertUndoBuffer == bufferId) {
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
    UndoHistory &history = buffer.undo;
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
    for (std::size_t index = insertionIndex;
         index < buffer.lineStarts.size();
         ++index) {
        if (inserted.size() >= removed) {
            buffer.lineStarts[index] +=
                inserted.size() - removed;
        } else {
            buffer.lineStarts[index] -=
                removed - inserted.size();
        }
    }

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
    const std::size_t boundedStart =
        std::min(start, buffer.text().size());
    const std::size_t boundedEnd =
        std::clamp(
            end,
            boundedStart,
            buffer.text().size());
    const std::size_t removed =
        boundedEnd - boundedStart;
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

    std::optional<std::size_t> undoNode;
    bool startedUndoNode = false;
    std::u16string removedText;
    if (recordUndo) {
        removedText = buffer.text().substr(
            boundedStart, removed);
        const bool groupWithInsert =
            insertUndoBuffer
            && *insertUndoBuffer == bufferId;
        const std::size_t nodeCount =
            buffer.undo.nodes.size();
        undoNode = ensureUndoNode(
            buffer,
            groupWithInsert,
            captureWindowCursors(bufferId));
        startedUndoNode =
            buffer.undo.nodes.size() != nodeCount;
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

    const auto adjustAnchor =
        [boundedStart, boundedEnd, removed,
         insertedSize = inserted.size()](
            const std::size_t oldOffset) {
            if (oldOffset > boundedEnd) {
                return oldOffset - removed
                    + insertedSize;
            }
            if (oldOffset >= boundedStart) {
                return boundedStart
                    + std::min(
                        oldOffset - boundedStart,
                        insertedSize);
            }
            return oldOffset;
        };
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
    if (!buffer.editText(
            boundedStart,
            removed,
            inserted)) {
        return false;
    }
    buffer.displayLines.clear();
    if (recordUndo) {
        buffer.lastChangeMark = boundedStart;
        recordChangePosition(
            buffer,
            bufferId,
            boundedStart,
            startedUndoNode,
            authoritativeCursor
                ? authoritativeCursor->first
                : authoritativeSelectionOffsets
                ? authoritativeSelectionOffsets->view
                : activeView);
        const std::size_t operationEnd = inserted.empty()
            ? boundedStart
            : boundedStart + inserted.size() - 1;
        if (undoNode
            && buffer.lastOperationUndoNode == undoNode) {
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
        UndoNode &node =
            buffer.undo.nodes[*undoNode];
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
        // Apply from the bottom upwards. Every edit offset is therefore
        // valid for the document state produced by the preceding edit,
        // and the entire block remains one open undo transaction.
        for (std::size_t line = block.lastLine;
             line > block.firstLine;
             --line) {
            auto found = buffers.find(block.buffer);
            if (found == buffers.end()
                || line >= found->second.lineStarts.size()) {
                continue;
            }
            const auto insertion = blockInsertionEdit(
                found->second,
                line,
                block.displayColumn,
                block.padShortLines);
            if (!insertion) {
                continue;
            }
            std::u16string replacement =
                insertion->replacement;
            replacement.insert(
                insertion->insertionOffset,
                activeInsertRepeat->insertedText);
            const std::size_t lineStart =
                found->second.lineStarts[line];
            (void)mutateBuffer(
                result,
                block.buffer,
                lineStart + insertion->bufferStart,
                lineStart + insertion->bufferEnd,
                std::move(replacement));
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
    if (foundBuffer == buffers.end()
        || nodeIndex
            >= foundBuffer->second.undo.nodes.size()) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const UndoNode &node =
        buffer.undo.nodes[nodeIndex];
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
    if (buffer.readOnly) {
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.buffer = bufferId;
        error.message = "buffer is read-only";
        result.events.push_back(std::move(error));
        return;
    }

    bool changedText = false;
    for (std::size_t step = 0;
         step < count;
         ++step) {
        UndoHistory &history = buffer.undo;
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
