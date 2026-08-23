#include "VkCoreInternal.h"

namespace vkui::vk {

void VkCore::Implementation::completeRepeatedInsert(
    DispatchResult &result,
    const WindowId windowId,
    const std::u16string &inserted,
    const bool closeUndo)
{
    const auto foundView = views.find(windowId);
    if (baseMode != Mode::Insert
        || foundView == views.end()
        || foundView->second.buffer == 0) {
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer =
        buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    if (!inserted.empty()) {
        const std::size_t insertion =
            offset(
                foundBuffer->second,
                view.cursors[view.buffer]);
        (void)mutateBuffer(
            &result,
            view.buffer,
            insertion,
            insertion,
            inserted);
    }
    const Mode before = effectiveMode();
    baseMode = Mode::Normal;
    if (closeUndo) {
        closeInsertUndoBlock();
    }
    const auto updatedBuffer =
        buffers.find(view.buffer);
    if (updatedBuffer != buffers.end()) {
        Cursor cursor = view.cursors[view.buffer];
        if (!inserted.empty() && cursor.column > 0) {
            cursor.column = previousColumnAllowEnd(
                updatedBuffer->second,
                cursor.line,
                cursor.column);
        }
        cursor = clampCursor(
            updatedBuffer->second, cursor, false);
        view.cursors[view.buffer] = cursor;
        view.preferredColumn.reset();
        emitCursor(result, windowId, view);
    }
    emitModeIfChanged(result, before);
}

void VkCore::Implementation::appendRepeatedOpenLines(
    DispatchResult &result,
    const WindowId windowId,
    const std::u16string &inserted,
    const std::size_t copies)
{
    if (copies == 0) {
        return;
    }
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.buffer == 0) {
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Cursor cursor = clampCursor(
        foundBuffer->second,
        view.cursors[view.buffer],
        false);
    const std::size_t insertion =
        foundBuffer->second.lineStarts[cursor.line]
        + lineLength(foundBuffer->second, cursor.line);
    std::u16string repeated;
    const std::size_t unit = inserted.size() + 1;
    if (copies
        <= std::numeric_limits<std::size_t>::max()
            / unit) {
        repeated.reserve(copies * unit);
    }
    for (std::size_t index = 0;
         index < copies;
         ++index) {
        repeated.push_back(u'\n');
        repeated += inserted;
    }
    if (!mutateBuffer(
            &result,
            view.buffer,
            insertion,
            insertion,
            repeated)) {
        return;
    }
    const auto updated = buffers.find(view.buffer);
    if (updated == buffers.end()) {
        return;
    }
    const std::size_t targetOffset =
        insertion + repeated.size();
    Cursor target = cursorAtOffset(
        updated->second, targetOffset, true);
    if (!inserted.empty()
        && target.column > 0) {
        target.column = previousColumnAllowEnd(
            updated->second,
            target.line,
            target.column);
    }
    view.cursors[view.buffer] =
        clampCursor(updated->second, target, false);
    view.preferredColumn.reset();
    emitCursor(result, windowId, view);
}

void VkCore::Implementation::repeatVisualBlock(
    DispatchResult &result,
    const WindowId windowId,
    const RepeatChange &repeat,
    const std::size_t commandCount,
    const bool countWasExplicit)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return;
    }
    View &view = foundView->second;
    auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor origin = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const std::size_t height =
        std::max<std::size_t>(1, repeat.blockHeight);
    // Neovim's redo-Visual path reuses the previous Visual geometry.
    // A count before dot does not stretch the remembered block width.
    static_cast<void>(commandCount);
    static_cast<void>(countWasExplicit);
    const std::size_t width =
        std::max<std::size_t>(1, repeat.blockWidth);
    const std::size_t originDisplayColumn =
        displayColumnForBufferColumn(
            buffer, origin.line, origin.column);
    const std::size_t lastLine = std::min(
        buffer.lineStarts.size() - 1,
        origin.line + height - 1);
    const std::size_t start =
        buffer.lineStarts[origin.line];
    const std::size_t end =
        buffer.lineStarts[lastLine]
        + lineLength(buffer, lastLine);
    std::u16string replacement;
    std::u16string selected;
    replacement.reserve(end - start
        + repeat.insertedText.size()
            * (lastLine - origin.line + 1));
    for (std::size_t line = origin.line;
         line <= lastLine;
         ++line) {
        BlockRowEdit row = blockRowEdit(
            buffer,
            line,
            originDisplayColumn,
            originDisplayColumn + width);
        selected += row.selected;
        if (repeat.operation == OperatorKind::Change) {
            const std::size_t lineWidth =
                displayLine(buffer, line).width;
            std::u16string inserted;
            if (originDisplayColumn > lineWidth) {
                inserted.append(
                    originDisplayColumn - lineWidth,
                    u' ');
            }
            inserted += repeat.insertedText;
            row.replacement.insert(
                row.insertionColumn, inserted);
        }
        replacement += row.replacement;
        if (line != lastLine) {
            selected.push_back(u'\n');
            replacement.push_back(u'\n');
        }
    }
    writeOperationRegister(
        repeat.operation,
        std::move(selected),
        false,
        true,
        width);
    (void)mutateBuffer(
        &result,
        view.buffer,
        start,
        end,
        std::move(replacement),
        std::pair<WindowId, Cursor>{
            windowId, origin});
}

void VkCore::Implementation::repeatVisualBlockInsert(
    DispatchResult &result,
    const WindowId windowId,
    const RepeatChange &repeat)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0
        || repeat.insertedText.empty()) {
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor origin = clampCursor(
        buffer, view.cursors[view.buffer], false);
    const std::size_t originDisplay =
        displayColumnForBufferColumn(
            buffer, origin.line, origin.column);
    const bool append =
        repeat.command == Command::AppendAtLineEnd;
    const std::size_t width =
        std::max<std::size_t>(1, repeat.blockWidth);
    const std::size_t displayColumn = append
        ? originDisplay
            + std::min(
                width,
                std::numeric_limits<std::size_t>::max()
                    - originDisplay)
        : originDisplay;
    const std::size_t lastLine = std::min(
        buffer.lineStarts.size() - 1,
        origin.line
            + std::min(
                std::max<std::size_t>(
                    1, repeat.blockHeight)
                    - 1,
                buffer.lineStarts.size() - 1
                    - origin.line));
    beginInsertUndoBlock(view.buffer);
    if (buffer.storage.isExternalSession()) {
        std::vector<vkui::buffer::TransactionEdit> edits;
        edits.reserve(lastLine - origin.line + 1);
        std::size_t targetOffset = offset(buffer, origin);
        for (std::size_t line = lastLine;; --line) {
            const auto insertion =
                blockInsertionEdit(buffer, line, displayColumn, append || line == origin.line);
            if (insertion) {
                std::u16string replacement = insertion->replacement;
                replacement.insert(insertion->insertionOffset, repeat.insertedText);
                const std::size_t lineStart = buffer.lineStarts[line];
                edits.push_back(vkui::buffer::TransactionEdit{
                    lineStart + insertion->bufferStart,
                    insertion->bufferEnd - insertion->bufferStart, std::move(replacement)});
                if (line == origin.line) {
                    targetOffset =
                        lineStart + insertion->resultingColumn + repeat.insertedText.size();
                }
            }
            if (line == origin.line) {
                break;
            }
        }
        const std::size_t beforeCursor = offset(buffer, view.cursors[view.buffer]);
        const vkui::buffer::TextSelection before{view.selectionAnchorOffset.value_or(beforeCursor),
                                                 beforeCursor};
        const auto group = externalEditGroups.find(view.buffer);
        const bool applied = mutateExternalBatch(
            &result, view.buffer, std::move(edits), before,
            vkui::buffer::TextSelection{targetOffset, targetOffset}, windowId,
            group == externalEditGroups.end() ? std::nullopt
                                              : std::optional<std::uint64_t>(group->second));
        closeInsertUndoBlock();
        if (applied) {
            view.preferredColumn.reset();
            emitCursor(result, windowId, view);
        }
        return;
    }
    for (std::size_t line = lastLine;; --line) {
        const auto insertion = blockInsertionEdit(
            buffer,
            line,
            displayColumn,
            append || line == origin.line);
        if (!insertion) {
            continue;
        }
        std::u16string replacement =
            insertion->replacement;
        replacement.insert(
            insertion->insertionOffset,
            repeat.insertedText);
        const std::size_t lineStart =
            buffer.lineStarts[line];
        const std::optional<std::pair<WindowId, Cursor>>
            cursor = line == origin.line
            ? std::optional<std::pair<WindowId, Cursor>>(
                  std::pair<WindowId, Cursor>{
                      windowId,
                      Cursor{
                          line,
                          insertion->resultingColumn
                              + repeat.insertedText.size()}})
            : std::nullopt;
        (void)mutateBuffer(
            &result,
            view.buffer,
            lineStart + insertion->bufferStart,
            lineStart + insertion->bufferEnd,
            std::move(replacement),
            cursor);
        if (line == origin.line) {
            break;
        }
    }
    closeInsertUndoBlock();
    view.preferredColumn.reset();
    emitCursor(result, windowId, view);
}

void VkCore::Implementation::repeatVisualBlockShape(
    DispatchResult &result,
    const WindowId windowId,
    const RepeatChange &repeat)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Cursor origin = clampCursor(
        foundBuffer->second,
        view.cursors[view.buffer],
        false);
    const std::size_t originDisplay =
        displayColumnForBufferColumn(
            foundBuffer->second,
            origin.line,
            origin.column);
    const std::size_t height =
        std::max<std::size_t>(1, repeat.blockHeight);
    const std::size_t width =
        std::max<std::size_t>(1, repeat.blockWidth);
    const std::size_t lastLine = std::min(
        foundBuffer->second.lineStarts.size() - 1,
        origin.line
            + std::min(
                height - 1,
                foundBuffer->second.lineStarts.size() - 1
                    - origin.line));
    const std::size_t lastColumn = originDisplay
        + std::min(
            width - 1,
            std::numeric_limits<std::size_t>::max()
                - originDisplay);
    baseMode = Mode::Visual;
    visualSelection = VisualSelection{
        windowId,
        view.buffer,
        origin,
        originDisplay,
        VisualKind::Block};
    setDisplayPosition(
        view,
        displayPositionForColumn(
            foundBuffer->second,
            lastLine,
            lastColumn));
    view.preferredColumn = lastColumn;

    if (repeat.form == RepeatForm::VisualBlockPut) {
        (void)putOverVisualBlock(
            result,
            windowId,
            repeat.put,
            repeat.command);
    } else if (
        repeat.form
            == RepeatForm::VisualBlockReplace) {
        (void)replaceVisualBlock(
            result,
            windowId,
            repeat.argument,
            false);
    } else {
        (void)toggleVisualBlock(
            result, windowId, false);
    }
    if (baseMode == Mode::Visual) {
        leaveVisualMode();
    }
}

void VkCore::Implementation::repeatVisualCharacter(
    DispatchResult &result,
    const WindowId windowId,
    const RepeatChange &repeat)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return;
    }
    View &view = foundView->second;
    auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor origin = clampCursor(
        buffer, view.cursors[view.buffer], false);
    const std::size_t start = offset(buffer, origin);
    std::size_t end = start;
    if (repeat.blockHeight <= 1) {
        const std::size_t endColumn = advanceWithinLine(
            buffer,
            origin.line,
            origin.column,
            std::max<std::size_t>(1, repeat.blockWidth));
        end = buffer.lineStarts[origin.line] + endColumn;
    } else {
        const std::size_t endLine = std::min(
            buffer.lineStarts.size() - 1,
            origin.line + repeat.blockHeight - 1);
        const std::size_t length = lineLength(buffer, endLine);
        const std::size_t endColumn = length == 0
            ? 0
            : nextColumnAllowEnd(
                  buffer,
                  endLine,
                  safeColumn(
                      buffer,
                      endLine,
                      std::min(
                          repeat.visualEndColumn,
                          length - 1),
                      false));
        end = buffer.lineStarts[endLine] + endColumn;
    }
    if (end <= start) {
        return;
    }
    writeOperationRegister(
        repeat.operation,
        buffer.text().substr(start, end - start),
        false);
    const std::u16string replacement =
        repeat.operation == OperatorKind::Change
        ? repeat.insertedText
        : std::u16string{};
    (void)mutateBuffer(
        &result,
        view.buffer,
        start,
        end,
        replacement,
        std::pair<WindowId, Cursor>{windowId, origin});
}

void VkCore::Implementation::repeatVisualTransform(
    DispatchResult &result,
    const WindowId windowId,
    const RepeatChange &repeat)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const Cursor origin = clampCursor(
        buffer, view.cursors[view.buffer], false);
    const std::size_t height =
        std::max<std::size_t>(1, repeat.blockHeight);
    const std::size_t lastLine = std::min(
        buffer.lineStarts.size() - 1,
        origin.line
            + std::min(
                height - 1,
                buffer.lineStarts.size() - 1
                    - origin.line));
    baseMode = Mode::Visual;
    const std::size_t originDisplay =
        displayColumnForBufferColumn(
            buffer, origin.line, origin.column);
    visualSelection = VisualSelection{
        windowId,
        view.buffer,
        origin,
        originDisplay,
        repeat.visualKind};

    if (repeat.visualKind == VisualKind::Line) {
        view.cursors[view.buffer] = Cursor{lastLine, 0};
    } else if (
        repeat.visualKind == VisualKind::Block) {
        const std::size_t width =
            std::max<std::size_t>(1, repeat.blockWidth);
        const std::size_t lastDisplay = originDisplay
            + std::min(
                width - 1,
                std::numeric_limits<std::size_t>::max()
                    - originDisplay);
        setDisplayPosition(
            view,
            displayPositionForColumn(
                buffer, lastLine, lastDisplay));
    } else if (height == 1) {
        const std::size_t width =
            std::max<std::size_t>(1, repeat.blockWidth);
        view.cursors[view.buffer] = Cursor{
            origin.line,
            advanceWithinLine(
                buffer,
                origin.line,
                origin.column,
                width - 1)};
    } else {
        const std::size_t length =
            lineLength(buffer, lastLine);
        view.cursors[view.buffer] = Cursor{
            lastLine,
            length == 0
                ? 0
                : safeColumn(
                      buffer,
                      lastLine,
                      std::min(
                          repeat.visualEndColumn,
                          length - 1),
                      false)};
    }
    view.preferredColumn.reset();
    (void)finishVisualTransform(
        result,
        windowId,
        repeat.operation,
        repeat.count,
        false);
}

void VkCore::Implementation::repeatLastChange(
    DispatchResult &result,
    const WindowId windowId,
    const std::size_t commandCount,
    const bool countWasExplicit)
{
    if (!lastChange) {
        return;
    }
    const RepeatChange repeat = *lastChange;
    struct SelectedRegisterReset final
    {
        char32_t &selected;
        ~SelectedRegisterReset()
        {
            selected = U'"';
        }
    } selectedRegisterReset{selectedRegister};
    selectedRegister = repeat.registerName;
    const std::size_t count =
        repeat.preserveVisualShape
        ? std::max<std::size_t>(1, repeat.count)
        : countWasExplicit
        ? commandCount
        : std::max<std::size_t>(1, repeat.count);
    replayingChange = true;

    if (repeat.form == RepeatForm::VisualTransform) {
        repeatVisualTransform(
            result, windowId, repeat);
        replayingChange = false;
        return;
    }

    if (repeat.form == RepeatForm::VisualCharacter) {
        repeatVisualCharacter(result, windowId, repeat);
        replayingChange = false;
        return;
    }

    if (repeat.form == RepeatForm::VisualBlock) {
        repeatVisualBlock(
            result,
            windowId,
            repeat,
            commandCount,
            countWasExplicit);
        replayingChange = false;
        return;
    }

    if (repeat.form == RepeatForm::VisualBlockInsert) {
        repeatVisualBlockInsert(
            result, windowId, repeat);
        replayingChange = false;
        return;
    }

    if (repeat.form == RepeatForm::VisualBlockPut
        || repeat.form
            == RepeatForm::VisualBlockReplace
        || repeat.form
            == RepeatForm::VisualBlockToggle) {
        repeatVisualBlockShape(
            result, windowId, repeat);
        replayingChange = false;
        return;
    }

    if (repeat.form
        == RepeatForm::OperatorLinewise
        || repeat.form
            == RepeatForm::OperatorMotion
        || repeat.form
            == RepeatForm::OperatorTextObject) {
        const auto foundView = views.find(windowId);
        if (foundView != views.end()
            && foundView->second.buffer != 0) {
            pendingOperator = PendingOperator{
                repeat.operation,
                foundView->second.buffer,
                foundView->second
                    .cursors[foundView->second.buffer],
                count,
                countWasExplicit
                    || repeat
                           .motionCountWasExplicit};
            normalCount = 0;
            if (repeat.form
                == RepeatForm::OperatorLinewise) {
                finishLinewiseOperator(
                    result, windowId);
            } else if (
                repeat.form
                == RepeatForm::OperatorTextObject) {
                applyTextObject(
                    result,
                    windowId,
                    repeat.textObject,
                    repeat.textObjectAround,
                    count);
            } else if (displayMotion(
                           repeat.motion)) {
                requestPendingOperatorDisplayMotion(
                    result,
                    windowId,
                    repeat.motion,
                    repeat.insertedText);
            } else {
                finishMotionOperator(
                    result,
                    windowId,
                    repeat.motion);
            }
        }
        if (repeat.operation
                == OperatorKind::Change
            && baseMode == Mode::Insert) {
            completeRepeatedInsert(
                result,
                windowId,
                repeat.insertedText);
        }
        replayingChange = false;
        return;
    }

    switch (repeat.command) {
    case Command::DeleteUnderCursor:
        (void)deleteCharacters(
            result,
            windowId,
            count,
            false,
            false,
            Command::DeleteUnderCursor);
        break;
    case Command::DeleteBeforeCursor:
        (void)deleteCharacters(
            result,
            windowId,
            count,
            true,
            false,
            Command::DeleteBeforeCursor);
        break;
    case Command::PutAfter:
    case Command::PutBefore: {
        const RegisterValue payload =
            registerValue(repeat.registerName);
        (void)put(
            result,
            windowId,
            payload,
            count,
            repeat.command == Command::PutAfter,
            false);
        break;
    }
    case Command::OpenBelow:
    case Command::OpenAbove:
        (void)openLine(
            result,
            windowId,
            1,
            repeat.command == Command::OpenBelow,
            repeat.command);
        completeRepeatedInsert(
            result,
            windowId,
            repeat.insertedText,
            false);
        appendRepeatedOpenLines(
            result,
            windowId,
            repeat.insertedText,
            count - 1);
        closeInsertUndoBlock();
        break;
    case Command::BeginReplace:
        (void)replaceCharacters(
            result,
            windowId,
            count,
            repeat.argument,
            false);
        break;
    case Command::EnterReplaceMode: {
        const auto foundView = views.find(windowId);
        if (foundView == views.end()
            || foundView->second.kind != ViewKind::Editor
            || foundView->second.buffer == 0
            || repeat.replaceActions.empty()) {
            break;
        }
        const BufferId bufferId =
            foundView->second.buffer;
        beginInsertUndoBlock(bufferId);
        baseMode = Mode::Replace;
        replaceSession = ReplaceSession{
            windowId,
            bufferId,
            1,
            false,
            {},
            repeat.replaceActions};
        replayReplaceActions(
            result,
            windowId,
            repeat.replaceActions,
            count);
        finishReplaceMode(
            nullptr, windowId, false);
        const auto updated = views.find(windowId);
        if (updated != views.end()) {
            emitCursor(
                result, windowId, updated->second);
        }
        break;
    }
    case Command::SubstituteCharacters:
        (void)deleteCharacters(
            result,
            windowId,
            count,
            false,
            true,
            Command::SubstituteCharacters);
        completeRepeatedInsert(
            result,
            windowId,
            repeat.insertedText);
        break;
    case Command::SubstituteLines:
        (void)substituteLines(
            result,
            windowId,
            count,
            Command::SubstituteLines);
        completeRepeatedInsert(
            result,
            windowId,
            repeat.insertedText);
        break;
    case Command::DeleteToLineEnd:
    case Command::ChangeToLineEnd:
        (void)changeToLineEnd(
            result,
            windowId,
            repeat.command
                == Command::ChangeToLineEnd,
            count,
            repeat.command);
        if (repeat.command
            == Command::ChangeToLineEnd) {
            completeRepeatedInsert(
                result,
                windowId,
                repeat.insertedText);
        }
        break;
    case Command::JoinLines:
        (void)joinLines(
            result,
            windowId,
            std::max<std::size_t>(2, count),
            false);
        break;
    case Command::ToggleCase:
        (void)toggleCase(
            result,
            windowId,
            count,
            false);
        break;
    case Command::AddNumber:
    case Command::SubtractNumber:
        (void)changeNumber(
            result,
            windowId,
            count,
            repeat.command == Command::SubtractNumber,
            false);
        break;
    case Command::EnterInsert:
    case Command::AppendInsert:
    case Command::InsertAtLineStart:
    case Command::AppendAtLineEnd:
        normalCount = 0;
        execute(
            result,
            windowId,
            repeat.command);
        {
            std::u16string repeatedText;
            if (!repeat.insertedText.empty()
                && count
                    <= std::numeric_limits<std::size_t>::max()
                        / repeat.insertedText.size()) {
                repeatedText.reserve(
                    repeat.insertedText.size()
                    * count);
            }
            for (std::size_t index = 0;
                 index < count;
                 ++index) {
                repeatedText +=
                    repeat.insertedText;
            }
            completeRepeatedInsert(
                result,
                windowId,
                repeatedText);
        }
        break;
    default:
        break;
    }
    replayingChange = false;
}

} // namespace vkui::vk
