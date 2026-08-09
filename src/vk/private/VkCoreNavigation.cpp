#include "VkCoreInternal.h"

namespace vkui::vk {

void VkCore::Implementation::applyTextObject(
    DispatchResult &result,
    const WindowId windowId,
    const char32_t object,
    const bool around,
    const std::size_t count)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        abortMacroCommand();
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        abortMacroCommand();
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }
    const Cursor cursor = clampCursor(
        foundBuffer->second,
        view.cursors[view.buffer],
        false);
    const auto range = textObjectRange(
        foundBuffer->second,
        cursor,
        object,
        around,
        count);
    if (!range || range->start >= range->end) {
        abortMacroCommand();
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }
    if (pendingOperator) {
        RepeatChange repeat;
        repeat.form = RepeatForm::OperatorTextObject;
        repeat.operation = pendingOperator->kind;
        repeat.count = count;
        repeat.textObject = object;
        repeat.textObjectAround = around;
        finishOperator(
            result,
            windowId,
            range->start,
            range->end,
            range->linewise,
            {},
            std::move(repeat));
        return;
    }
    if (baseMode != Mode::Visual
        || !visualSelection) {
        return;
    }
    visualSelection->anchor = cursorAtOffset(
        foundBuffer->second,
        range->start);
    visualSelection->anchorDisplayColumn =
        displayColumnForBufferColumn(
            foundBuffer->second,
            visualSelection->anchor.line,
            visualSelection->anchor.column);
    visualSelection->kind = range->linewise
        ? VisualKind::Line
        : VisualKind::Character;
    view.cursors[view.buffer] = cursorAtOffset(
        foundBuffer->second,
        range->end - 1);
    view.displayColumns[view.buffer] =
        displayColumnForBufferColumn(
            foundBuffer->second,
            view.cursors[view.buffer].line,
            view.cursors[view.buffer].column);
    view.preferredColumn.reset();
    emitCursor(result, windowId, view);
}

void VkCore::Implementation::leaveVisualMode()
{
    if (baseMode == Mode::Visual && visualSelection) {
        const auto foundView = views.find(
            visualSelection->window);
        const auto foundBuffer = buffers.find(
            visualSelection->buffer);
        if (foundView != views.end()
            && foundBuffer != buffers.end()
            && foundView->second.buffer
                == visualSelection->buffer) {
            Buffer &buffer = foundBuffer->second;
            const Cursor cursor = clampCursor(
                buffer,
                foundView->second.cursors[
                    visualSelection->buffer],
                false);
            const std::size_t anchorOffset = offset(
                buffer, visualSelection->anchor);
            const std::size_t cursorOffset = offset(
                buffer, cursor);
            buffer.lastVisualStartMark = std::min(
                anchorOffset, cursorOffset);
            buffer.lastVisualEndMark = std::max(
                anchorOffset, cursorOffset);
        }
    }
    // Visual block may legally carry a coladd-style display position
    // inside a tab or beyond EOL. Normal mode cannot represent that
    // position, so collapse it to the nearest real character before the
    // selection owner is discarded. Block change switches to Insert
    // first and intentionally keeps its insertion column.
    if (baseMode == Mode::Visual && visualSelection) {
        const auto foundView = views.find(
            visualSelection->window);
        if (foundView != views.end()
            && foundView->second.buffer
                == visualSelection->buffer) {
            View &view = foundView->second;
            const auto foundBuffer = buffers.find(
                view.buffer);
            if (foundBuffer != buffers.end()) {
                const DisplayPosition current =
                    currentDisplayPosition(
                        view, foundBuffer->second);
                const Cursor cursor =
                    normalCursorForDisplayColumn(
                        foundBuffer->second,
                        current.buffer.line,
                        current.column);
                view.cursors[view.buffer] = cursor;
                view.displayColumns[view.buffer] =
                    displayColumnForBufferColumn(
                        foundBuffer->second,
                        cursor.line,
                        cursor.column);
                view.preferredColumn.reset();
            }
        }
    }
    visualSelection.reset();
    if (baseMode == Mode::Visual) {
        baseMode = Mode::Normal;
    }
}

[[nodiscard]] bool VkCore::Implementation::cancelStaleVisual(
    DispatchResult &result,
    const WindowId windowId)
{
    if (baseMode != Mode::Visual) {
        return false;
    }
    const auto foundView = views.find(windowId);
    if (visualSelection
        && foundView != views.end()
        && foundView->second.kind == ViewKind::Editor
        && foundView->second.buffer != 0
        && visualSelection->window == windowId
        && visualSelection->buffer
            == foundView->second.buffer) {
        return false;
    }

    const Mode before = effectiveMode();
    leaveVisualMode();
    pendingOperator.reset();
    pendingArgument.reset();
    normalCount = 0;
    prefix = CommandPrefix::None;
    emitModeIfChanged(result, before);
    return true;
}

void VkCore::Implementation::activateRelativeBuffer(
    DispatchResult &result,
    const View &view,
    const ViewId viewId,
    const int direction)
{
    if (bufferOrder.empty()) {
        return;
    }
    auto current = std::find(
        bufferOrder.cbegin(), bufferOrder.cend(), view.buffer);
    const auto count =
        static_cast<std::ptrdiff_t>(bufferOrder.size());
    std::size_t index = current == bufferOrder.cend()
        ? direction > 0
            ? bufferOrder.size() - 1
            : 0
        : static_cast<std::size_t>(
              std::distance(bufferOrder.cbegin(), current));
    index = static_cast<std::size_t>(
        (static_cast<std::ptrdiff_t>(index)
         + direction + count) % count);
    // Buffer activation is a host transaction: the current document may
    // first need to save, resolve a conflict, or reject navigation. Keep
    // the core view unchanged until the host successfully loads the target
    // and commits it through synchronizeBuffer().
    Event requested;
    requested.type =
        EventType::BufferActivationRequested;
    requested.view = viewId;
    requested.buffer = bufferOrder[index];
    result.events.push_back(std::move(requested));
}

void VkCore::Implementation::activateAlternateBuffer(
    DispatchResult &result,
    const View &view,
    const ViewId viewId,
    const std::size_t count,
    const bool countWasExplicit)
{
    const BufferId target = countWasExplicit
        ? static_cast<BufferId>(count)
        : view.alternateBuffer;
    if (target == 0 || !buffers.contains(target)) {
        Event error;
        error.type = EventType::InputError;
        error.view = viewId;
        error.buffer = view.buffer;
        error.message = countWasExplicit
            ? "requested buffer does not exist"
            : "alternate buffer is not set";
        result.events.push_back(std::move(error));
        return;
    }
    if (target == view.buffer) {
        return;
    }
    Event requested;
    requested.type = EventType::BufferActivationRequested;
    requested.view = viewId;
    requested.buffer = target;
    result.events.push_back(std::move(requested));
}

[[nodiscard]] Cursor VkCore::Implementation::movedCursor(
    View &view,
    const Command command,
    const std::size_t count,
    const bool countWasExplicit) const
{
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return view.cursors[view.buffer];
    }
    const Buffer &buffer = foundBuffer->second;
    Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    switch (command) {
    case Command::MoveLeft:
        for (std::size_t index = 0;
             index < count;
             ++index) {
            cursor.column = previousColumn(
                buffer, cursor.line, cursor.column);
        }
        view.preferredColumn.reset();
        break;
    case Command::MoveRight:
        for (std::size_t index = 0;
             index < count;
             ++index) {
            cursor.column = nextColumn(
                buffer, cursor.line, cursor.column);
        }
        view.preferredColumn.reset();
        break;
    case Command::MoveUp:
    case Command::MoveDown: {
        const DisplayPosition display =
            currentDisplayPosition(view, buffer);
        const std::size_t goal =
            view.preferredColumn.value_or(
                display.column);
        view.preferredColumn = goal;
        if (command == Command::MoveUp) {
            cursor.line = cursor.line > count
                ? cursor.line - count
                : 0;
        } else {
            cursor.line = std::min(
                buffer.lineStarts.size() - 1,
                cursor.line + count);
        }
        cursor = normalCursorForDisplayColumn(
            buffer, cursor.line, goal);
        break;
    }
    case Command::LineStart:
        cursor.column = 0;
        view.preferredColumn.reset();
        break;
    case Command::FirstNonBlank:
        cursor.column = firstNonBlankColumn(
            buffer, cursor.line);
        view.preferredColumn.reset();
        break;
    case Command::LineEnd:
        cursor.column =
            normalColumnLimit(buffer, cursor.line);
        view.preferredColumn.reset();
        break;
    case Command::LastNonBlank:
        cursor.line = std::min(
            buffer.lineStarts.size() - 1,
            cursor.line + count - 1);
        cursor.column = lastNonBlankColumn(
            buffer, cursor.line);
        view.preferredColumn.reset();
        break;
    case Command::LineDownFirstNonBlank:
        cursor.line = std::min(
            buffer.lineStarts.size() - 1,
            cursor.line + count);
        cursor.column = firstNonBlankColumn(
            buffer, cursor.line);
        view.preferredColumn.reset();
        break;
    case Command::LineUpFirstNonBlank:
        cursor.line = cursor.line > count
            ? cursor.line - count
            : 0;
        cursor.column = firstNonBlankColumn(
            buffer, cursor.line);
        view.preferredColumn.reset();
        break;
    case Command::CurrentLineFirstNonBlank:
        cursor.line = std::min(
            buffer.lineStarts.size() - 1,
            cursor.line + count - 1);
        cursor.column = firstNonBlankColumn(
            buffer, cursor.line);
        view.preferredColumn.reset();
        break;
    case Command::ScreenColumn:
        cursor = normalCursorForDisplayColumn(
            buffer, cursor.line, count - 1);
        view.preferredColumn.reset();
        break;
    case Command::FirstLine:
        cursor.line = countWasExplicit
            ? std::min(
                  buffer.lineStarts.size() - 1,
                  count - 1)
            : 0;
        cursor.column = firstNonBlankColumn(
            buffer, cursor.line);
        view.preferredColumn.reset();
        break;
    case Command::FilePercent:
        if (countWasExplicit && count <= 100) {
            const std::size_t lineCount =
                buffer.lineStarts.size();
            // Neovim's ({count} * line-count + 99) / 100 formula,
            // written without a potentially overflowing product.
            const std::size_t targetOneBased =
                (lineCount / 100) * count
                + ((lineCount % 100) * count + 99)
                    / 100;
            cursor.line = std::min(
                lineCount - 1,
                std::max<std::size_t>(
                    1, targetOneBased) - 1);
            cursor.column = firstNonBlankColumn(
                buffer, cursor.line);
            view.preferredColumn.reset();
        }
        break;
    case Command::TextMiddle: {
        const std::size_t percentage =
            countWasExplicit ? count : 50;
        if (percentage <= 100) {
            const std::size_t width =
                displayLine(buffer, cursor.line).width;
            const std::size_t goal =
                (width / 100) * percentage
                + ((width % 100) * percentage) / 100;
            cursor = normalCursorForDisplayColumn(
                buffer, cursor.line, goal);
            view.preferredColumn.reset();
        }
        break;
    }
    case Command::LastLine:
        cursor.line = countWasExplicit
            ? std::min(
                  buffer.lineStarts.size() - 1,
                  count - 1)
            : buffer.lineStarts.size() - 1;
        cursor.column = firstNonBlankColumn(
            buffer, cursor.line);
        view.preferredColumn.reset();
        break;
    case Command::WindowTop:
    case Command::WindowMiddle:
    case Command::WindowBottom: {
        const std::size_t lastLine =
            buffer.lineStarts.size() - 1;
        const std::size_t top =
            std::min(view.topline, lastLine);
        const std::size_t visibleRows =
            std::max<std::size_t>(
                1, view.viewportRows);
        const std::size_t bottom =
            top + std::min(
                      lastLine - top,
                      visibleRows - 1);
        if (command == Command::WindowTop) {
            cursor.line =
                top + std::min(
                          bottom - top,
                          count - 1);
        } else if (
            command == Command::WindowBottom) {
            cursor.line =
                bottom - std::min(
                             bottom - top,
                             count - 1);
        } else {
            cursor.line =
                top + (bottom - top) / 2;
        }
        cursor.column = firstNonBlankColumn(
            buffer, cursor.line);
        view.preferredColumn.reset();
        break;
    }
    case Command::BigWordForward:
    case Command::BigWordBackward:
    case Command::BigWordEnd:
        cursor = movedWordCursor(
            buffer,
            cursor,
            command,
            count,
            true);
        view.preferredColumn.reset();
        break;
    case Command::BigWordEndBackward:
        cursor = movedWordEndBackward(
            buffer, cursor, count, true);
        view.preferredColumn.reset();
        break;
    case Command::WordForward:
    case Command::WordBackward:
    case Command::WordEnd:
        cursor = movedWordCursor(
            buffer,
            cursor,
            command,
            count,
            false);
        view.preferredColumn.reset();
        break;
    case Command::WordEndBackward:
        cursor = movedWordEndBackward(
            buffer, cursor, count, false);
        view.preferredColumn.reset();
        break;
    case Command::SentenceBackward:
    case Command::SentenceForward:
        cursor = movedSentenceCursor(
            buffer,
            cursor,
            count,
            command == Command::SentenceForward);
        view.preferredColumn.reset();
        break;
    case Command::ParagraphBackward:
    case Command::ParagraphForward:
        cursor = movedParagraphCursor(
            buffer,
            cursor,
            count,
            command == Command::ParagraphForward);
        view.preferredColumn.reset();
        break;
    case Command::SectionBackwardStart:
    case Command::SectionForwardStart:
    case Command::SectionBackwardEnd:
    case Command::SectionForwardEnd:
        cursor = movedSectionCursor(
            buffer,
            cursor,
            count,
            command == Command::SectionForwardStart
                || command == Command::SectionForwardEnd,
            command == Command::SectionBackwardEnd
                || command == Command::SectionForwardEnd);
        view.preferredColumn.reset();
        break;
    default:
        break;
    }
    return cursor;
}

void VkCore::Implementation::moveCursor(
    DispatchResult &result,
    View &view,
    const ViewId viewId,
    const Command command,
    const std::size_t count,
    const bool countWasExplicit)
{
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer != buffers.end()
        && ownsVisualBlock(viewId, view.buffer)
        && (command == Command::MoveLeft
            || command == Command::MoveRight
            || command == Command::MoveUp
            || command == Command::MoveDown)) {
        DisplayPosition origin =
            currentDisplayPosition(
                view, foundBuffer->second);
        const bool vertical =
            command == Command::MoveUp
            || command == Command::MoveDown;
        const std::size_t goal = vertical
            ? view.preferredColumn.value_or(
                  origin.column)
            : origin.column;
        if (vertical) {
            origin.column = goal;
        }
        const DisplayPosition destination =
            movedVisualBlockPosition(
                foundBuffer->second,
                origin,
                command,
                count);
        setDisplayPosition(view, destination);
        view.preferredColumn = vertical
            ? goal
            : destination.column;
        emitCursor(result, viewId, view);
        return;
    }
    const Cursor origin = foundBuffer == buffers.end()
        ? Cursor{}
        : clampCursor(
              foundBuffer->second,
              view.cursors[view.buffer],
              false);
    const Cursor destination = movedCursor(
        view,
        command,
        count,
        countWasExplicit);
    if (foundBuffer != buffers.end()
        && origin != destination
        && (command == Command::FirstLine
            || command == Command::LastLine
            || command == Command::SentenceBackward
            || command == Command::SentenceForward
            || command == Command::ParagraphBackward
            || command == Command::ParagraphForward
            || command == Command::SectionBackwardStart
            || command == Command::SectionForwardStart
            || command == Command::SectionBackwardEnd
            || command == Command::SectionForwardEnd
            || command == Command::FilePercent
            || command == Command::WindowTop
            || command == Command::WindowMiddle
            || command == Command::WindowBottom)) {
        recordJump(
            view,
            Location{
                view.buffer,
                offset(foundBuffer->second, origin)},
            Location{
                view.buffer,
                offset(foundBuffer->second, destination)});
    }
    view.cursors[view.buffer] = destination;
    if (foundBuffer != buffers.end()) {
        view.displayColumns[view.buffer] =
            displayColumnForBufferColumn(
                foundBuffer->second,
                destination.line,
                destination.column);
    }
    emitCursor(result, viewId, view);
}

void VkCore::Implementation::scrollHalfPage(
    DispatchResult &result,
    View &view,
    const WindowId windowId,
    const bool forward,
    const std::size_t count,
    const bool countWasExplicit)
{
    if (view.kind == ViewKind::Navigation
        || view.kind == ViewKind::Surface
        || view.viewportMotionAuthority
            == ViewportMotionAuthority::HostVisual) {
        const std::size_t viewportRows =
            std::max<std::size_t>(1, view.viewportRows);
        const std::size_t rows = countWasExplicit
            ? std::max<std::size_t>(1, count)
            : std::max<std::size_t>(1, viewportRows / 2);
        // One typed transaction lets a structured renderer measure the
        // visual destination once. In particular, PDF glyph rows are not
        // interchangeable with extracted-text lines; mutating the logical
        // cursor here and asking the renderer to reveal it caused two jumps.
        emitHost(
            result,
            forward
                ? HostAction::NavigateHalfPageDown
                : HostAction::NavigateHalfPageUp,
            rows,
            countWasExplicit);
        return;
    }
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const std::size_t viewportRows =
        std::max<std::size_t>(1, view.viewportRows);
    if (countWasExplicit) {
        view.scrollRows = std::min(
            viewportRows,
            std::max<std::size_t>(1, count));
    }
    const std::size_t rows =
        view.scrollRows != 0
        ? std::min(viewportRows, view.scrollRows)
        : std::max<std::size_t>(
              1, viewportRows / 2);
    const std::size_t lineCount =
        buffer.lineStarts.size();
    const std::size_t maxTopline =
        lineCount > viewportRows
        ? lineCount - viewportRows
        : 0;
    if (forward) {
        view.topline = std::min(
            maxTopline, view.topline + rows);
    } else {
        view.topline = view.topline > rows
            ? view.topline - rows
            : 0;
    }

    Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const DisplayPosition display =
        currentDisplayPosition(view, buffer);
    const std::size_t goal =
        view.preferredColumn.value_or(display.column);
    view.preferredColumn = goal;
    if (forward) {
        cursor.line = std::min(
            lineCount - 1, cursor.line + rows);
    } else {
        cursor.line = cursor.line > rows
            ? cursor.line - rows
            : 0;
    }
    if (ownsVisualBlock(windowId, view.buffer)) {
        const std::size_t actual =
            virtualEdit == VirtualEditMode::Block
            ? goal
            : std::min(
                  goal,
                  displayLine(buffer, cursor.line).width);
        setDisplayPosition(
            view,
            displayPositionForColumn(
                buffer, cursor.line, actual));
    } else {
        cursor = normalCursorForDisplayColumn(
            buffer, cursor.line, goal);
        view.cursors[view.buffer] = cursor;
        view.displayColumns[view.buffer] =
            displayColumnForBufferColumn(
                buffer, cursor.line, cursor.column);
    }
    emitViewport(result, windowId, view);
    emitCursor(result, windowId, view);
}

void VkCore::Implementation::scrollFullPage(
    DispatchResult &result,
    View &view,
    const WindowId windowId,
    const bool forward,
    const std::size_t count)
{
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const std::size_t viewportRows =
        std::max<std::size_t>(1, view.viewportRows);
    const std::size_t lineCount =
        buffer.lineStarts.size();
    const std::size_t maxTopline =
        lineCount > viewportRows
        ? lineCount - viewportRows
        : 0;
    view.topline = std::min(
        view.topline, maxTopline);
    const std::size_t requestedRows =
        count > std::numeric_limits<std::size_t>::max()
                    / viewportRows
        ? std::numeric_limits<std::size_t>::max()
        : count * viewportRows;
    const std::size_t oldTopline = view.topline;
    if (forward) {
        view.topline += std::min(
            requestedRows,
            maxTopline - view.topline);
    } else {
        view.topline -= std::min(
            requestedRows, view.topline);
    }
    const std::size_t movedRows =
        forward
        ? view.topline - oldTopline
        : oldTopline - view.topline;

    Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const DisplayPosition display =
        currentDisplayPosition(view, buffer);
    const std::size_t goal =
        view.preferredColumn.value_or(display.column);
    view.preferredColumn = goal;
    if (forward) {
        cursor.line += std::min(
            movedRows,
            lineCount - 1 - cursor.line);
    } else {
        cursor.line -= std::min(
            movedRows, cursor.line);
    }
    if (ownsVisualBlock(windowId, view.buffer)) {
        const std::size_t actual =
            virtualEdit == VirtualEditMode::Block
            ? goal
            : std::min(
                  goal,
                  displayLine(buffer, cursor.line).width);
        setDisplayPosition(
            view,
            displayPositionForColumn(
                buffer, cursor.line, actual));
    } else {
        cursor = normalCursorForDisplayColumn(
            buffer, cursor.line, goal);
        view.cursors[view.buffer] = cursor;
        view.displayColumns[view.buffer] =
            displayColumnForBufferColumn(
                buffer, cursor.line, cursor.column);
    }
    emitViewport(result, windowId, view);
    emitCursor(result, windowId, view);
}

void VkCore::Implementation::scrollSingleLine(
    DispatchResult &result,
    View &view,
    const WindowId windowId,
    const bool forward,
    const std::size_t count)
{
    if (view.viewportMotionAuthority
        == ViewportMotionAuthority::HostVisual) {
        emitHost(
            result,
            forward
                ? HostAction::ScrollViewportLineDown
                : HostAction::ScrollViewportLineUp,
            count,
            count > 1);
        return;
    }
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const std::size_t viewportRows =
        std::max<std::size_t>(1, view.viewportRows);
    const std::size_t lineCount =
        buffer.lineStarts.size();
    const std::size_t maxTopline =
        lineCount > viewportRows
        ? lineCount - viewportRows
        : 0;
    view.topline = std::min(
        view.topline, maxTopline);
    if (forward) {
        view.topline += std::min(
            count, maxTopline - view.topline);
    } else {
        view.topline -= std::min(
            count, view.topline);
    }

    Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const DisplayPosition display =
        currentDisplayPosition(view, buffer);
    const std::size_t goal =
        view.preferredColumn.value_or(display.column);
    const std::size_t bottom =
        std::min(
            lineCount - 1,
            view.topline + viewportRows - 1);
    cursor.line = std::clamp(
        cursor.line, view.topline, bottom);
    if (ownsVisualBlock(windowId, view.buffer)) {
        const std::size_t actual =
            virtualEdit == VirtualEditMode::Block
            ? goal
            : std::min(
                  goal,
                  displayLine(buffer, cursor.line).width);
        setDisplayPosition(
            view,
            displayPositionForColumn(
                buffer, cursor.line, actual));
    } else {
        cursor = normalCursorForDisplayColumn(
            buffer, cursor.line, goal);
        view.cursors[view.buffer] = cursor;
        view.displayColumns[view.buffer] =
            displayColumnForBufferColumn(
                buffer, cursor.line, cursor.column);
    }
    emitViewport(result, windowId, view);
    emitCursor(result, windowId, view);
}

void VkCore::Implementation::positionViewport(
    DispatchResult &result,
    View &view,
    const WindowId windowId,
    const Command command)
{
    if (view.viewportMotionAuthority
        == ViewportMotionAuthority::HostVisual) {
        const HostAction action =
            command == Command::ViewCursorAtCenter
            ? HostAction::PositionViewportCursorCenter
            : command == Command::ViewCursorAtBottom
            ? HostAction::PositionViewportCursorBottom
            : HostAction::PositionViewportCursorTop;
        emitHost(result, action);
        return;
    }
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const std::size_t viewportRows =
        std::max<std::size_t>(1, view.viewportRows);
    const std::size_t lineCount =
        buffer.lineStarts.size();
    const std::size_t maxTopline =
        lineCount > viewportRows
        ? lineCount - viewportRows
        : 0;
    const Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    std::size_t desired = cursor.line;
    if (command == Command::ViewCursorAtCenter) {
        desired = cursor.line
            > viewportRows / 2
            ? cursor.line - viewportRows / 2
            : 0;
    } else if (command == Command::ViewCursorAtBottom) {
        const std::size_t bottomOffset =
            viewportRows - 1;
        desired = cursor.line > bottomOffset
            ? cursor.line - bottomOffset
            : 0;
    }
    view.topline = std::min(
        desired, maxTopline);
    emitViewport(result, windowId, view);
}

void VkCore::Implementation::scrollHorizontalViewport(
    DispatchResult &result,
    View &view,
    const WindowId windowId,
    const Command command,
    const std::size_t count,
    const bool countWasExplicit)
{
    if (view.viewportMotionAuthority
        == ViewportMotionAuthority::HostVisual) {
        HostAction action = HostAction::ScrollViewportLeft;
        switch (command) {
        case Command::ViewScrollRight:
            action = HostAction::ScrollViewportRight;
            break;
        case Command::ViewScrollHalfLeft:
            action = HostAction::ScrollViewportHalfLeft;
            break;
        case Command::ViewScrollHalfRight:
            action = HostAction::ScrollViewportHalfRight;
            break;
        case Command::ViewCursorAtStart:
            action = HostAction::PositionViewportCursorStart;
            break;
        case Command::ViewCursorAtEnd:
            action = HostAction::PositionViewportCursorEnd;
            break;
        default:
            break;
        }
        emitHost(result, action, count, countWasExplicit);
        return;
    }

    // Logical text views retain horizontal state in display columns. The
    // host-visual path above uses measured pixels; the core deliberately
    // never guesses PDF glyph geometry.
    const std::size_t viewportColumns =
        view.viewportColumns == 0 ? 80 : view.viewportColumns;
    const std::size_t amount =
        command == Command::ViewScrollHalfLeft
            || command == Command::ViewScrollHalfRight
        ? std::max<std::size_t>(1, viewportColumns / 2) * count
        : count;
    if (command == Command::ViewScrollLeft
        || command == Command::ViewScrollHalfLeft) {
        view.leftColumn -= std::min(view.leftColumn, amount);
    } else if (command == Command::ViewScrollRight
               || command == Command::ViewScrollHalfRight) {
        view.leftColumn += amount;
    } else {
        const auto foundBuffer = buffers.find(view.buffer);
        if (foundBuffer == buffers.end()) {
            return;
        }
        const DisplayPosition cursor = currentDisplayPosition(
            view, foundBuffer->second);
        if (command == Command::ViewCursorAtStart) {
            view.leftColumn = cursor.column;
        } else if (cursor.column >= viewportColumns - 1) {
            view.leftColumn = cursor.column - (viewportColumns - 1);
        } else {
            view.leftColumn = 0;
        }
    }
    emitViewport(result, windowId, view);
}

void VkCore::Implementation::beginCharacterSearchArgument(
    const WindowId windowId,
    const std::size_t count,
    const bool countWasExplicit,
    const bool forward,
    const bool till)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0
        || !buffers.contains(
            foundView->second.buffer)) {
        return;
    }
    pendingArgument = PendingArgument{
        PendingArgumentKind::CharacterSearch,
        windowId,
        foundView->second.buffer,
        count,
        countWasExplicit,
        forward,
        till};
}

void VkCore::Implementation::requestDisplayMotion(
    DispatchResult &result,
    const WindowId windowId,
    const Command command,
    std::size_t count,
    const bool countWasExplicit,
    std::u16string repeatedInsert)
{
    const auto foundView = views.find(windowId);
    if (!displayMotion(command)
        || pendingDisplayMotion
        || foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const Cursor origin = pendingOperator
        ? pendingOperator->start
        : clampCursor(
              buffer,
              view.cursors[view.buffer],
              false);
    DisplayPosition source{
        origin,
        displayColumnForBufferColumn(
            buffer, origin.line, origin.column)};
    if (!pendingOperator
        && ownsVisualBlock(windowId, view.buffer)) {
        source = currentDisplayPosition(view, buffer);
    }
    if (!verticalDisplayMotion(command)
        && command != Command::DisplayRowEnd) {
        count = 1;
    }

    DisplayMotionRequestId request =
        nextDisplayMotionRequest++;
    if (request == 0) {
        request = nextDisplayMotionRequest++;
    }
    pendingDisplayMotion = PendingDisplayMotion{
        request,
        windowId,
        inputContextTarget != 0
            ? inputContextTarget
            : static_cast<InputTargetId>(windowId),
        view.buffer,
        buffer.revision(),
        origin,
        command,
        std::max<std::size_t>(1, count),
        countWasExplicit,
        pendingOperator.has_value(),
        std::move(repeatedInsert)};

    Event event;
    event.type = EventType::DisplayMotionRequested;
    event.view = windowId;
    event.inputTarget =
        pendingDisplayMotion->inputTarget;
    event.buffer = view.buffer;
    event.mode = effectiveMode();
    event.cursor = origin;
    event.hasCursor = true;
    event.count = pendingDisplayMotion->count;
    event.countWasExplicit = countWasExplicit;
    event.displayMotionRequest = request;
    event.displayBufferRevision =
        buffer.revision();
    event.displayMotionKind = displayMotionKind(command);
    event.displayPosition = source;
    if (verticalDisplayMotion(command)) {
        event.displayRowGoalColumn =
            view.preferredDisplayRowColumn;
    }
    result.events.push_back(std::move(event));
}

void VkCore::Implementation::requestPendingOperatorDisplayMotion(
    DispatchResult &result,
    const WindowId windowId,
    const Command command,
    std::u16string repeatedInsert)
{
    if (!pendingOperator) {
        return;
    }
    const std::size_t multiplier =
        std::max<std::size_t>(1, normalCount);
    const std::size_t count =
        pendingOperator->count > 999'999'999 / multiplier
        ? 999'999'999
        : pendingOperator->count * multiplier;
    const bool countWasExplicit =
        pendingOperator->countWasExplicit
        || normalCount != 0;
    normalCount = 0;
    requestDisplayMotion(
        result,
        windowId,
        command,
        count,
        countWasExplicit,
        std::move(repeatedInsert));
}

} // namespace vkui::vk
