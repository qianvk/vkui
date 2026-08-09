#include "VkCoreInternal.h"

namespace vkui::vk {

[[nodiscard]] std::u16string VkCore::Implementation::linewiseText(
    const Buffer &buffer,
    const std::size_t firstLine,
    const std::size_t lastLine) const
{
    std::u16string text;
    for (std::size_t line = firstLine;
         line <= lastLine;
         ++line) {
        text.append(
            buffer.text(),
            buffer.lineStarts[line],
            lineLength(buffer, line));
        text.push_back(u'\n');
    }
    return text;
}

[[nodiscard]] std::optional<BlockInsertionEdit>
VkCore::Implementation::blockInsertionEdit(
    const Buffer &buffer,
    const std::size_t line,
    const std::size_t displayColumn,
    const bool padShortLine) const
{
    const DisplayLine &layout = displayLine(buffer, line);
    if (displayColumn > layout.width) {
        if (!padShortLine) {
            return std::nullopt;
        }
        BlockInsertionEdit edit;
        edit.bufferStart = layout.bufferLength;
        edit.bufferEnd = layout.bufferLength;
        edit.replacement.assign(
            displayColumn - layout.width, u' ');
        edit.insertionOffset = edit.replacement.size();
        edit.resultingColumn = layout.bufferLength
            + edit.insertionOffset;
        return edit;
    }

    const DisplayBoundary boundary = displayBoundary(
        buffer, line, displayColumn);
    BlockInsertionEdit edit;
    edit.bufferStart = boundary.bufferColumn;
    edit.bufferEnd = boundary.cellBufferEnd;
    if (boundary.bufferColumn
        == boundary.cellBufferEnd) {
        edit.resultingColumn = boundary.bufferColumn;
        return edit;
    }

    edit.bufferStart = boundary.bufferColumn;
    edit.bufferEnd = boundary.cellBufferEnd;
    edit.replacement.assign(
        boundary.cellDisplayEnd
            - boundary.cellDisplayStart,
        u' ');
    edit.insertionOffset = displayColumn
        - boundary.cellDisplayStart;
    edit.resultingColumn = edit.bufferStart
        + edit.insertionOffset;
    return edit;
}

[[nodiscard]] VkCore::Implementation::BlockRowEdit VkCore::Implementation::blockRowEdit(
    const Buffer &buffer,
    const std::size_t line,
    const std::size_t firstDisplayColumn,
    const std::size_t lastDisplayColumnExclusive) const
{
    BlockRowEdit edit;
    const std::size_t lineStart = buffer.lineStarts[line];
    const std::size_t length = lineLength(buffer, line);
    const DisplayLine &layout = displayLine(buffer, line);
    const std::size_t requestedWidth =
        lastDisplayColumnExclusive > firstDisplayColumn
        ? lastDisplayColumnExclusive - firstDisplayColumn
        : 0;
    edit.replacement.reserve(length + requestedWidth);
    edit.selected.reserve(requestedWidth);
    bool insertionResolved = false;
    std::size_t selectedCells = 0;

    const auto processLinearRun =
        [&](const std::size_t bufferStart,
            const std::size_t bufferEnd,
            const std::size_t displayStart) {
            const std::size_t displayEnd =
                displayStart
                + (bufferEnd - bufferStart);
            if (displayEnd <= firstDisplayColumn
                || displayStart
                    >= lastDisplayColumnExclusive) {
                if (!insertionResolved
                    && displayStart
                        >= firstDisplayColumn) {
                    edit.insertionColumn =
                        edit.replacement.size();
                    insertionResolved = true;
                }
                edit.replacement.append(
                    buffer.text(),
                    lineStart + bufferStart,
                    bufferEnd - bufferStart);
                return;
            }

            const std::size_t selectedStart = std::max(
                firstDisplayColumn, displayStart);
            const std::size_t selectedEnd = std::min(
                lastDisplayColumnExclusive,
                displayEnd);
            const std::size_t prefix =
                selectedStart - displayStart;
            const std::size_t selectedWidth =
                selectedEnd - selectedStart;
            const std::size_t suffix =
                displayEnd - selectedEnd;
            edit.replacement.append(
                buffer.text(),
                lineStart + bufferStart,
                prefix);
            if (!insertionResolved) {
                edit.insertionColumn =
                    edit.replacement.size();
                insertionResolved = true;
            }
            edit.selected.append(
                buffer.text(),
                lineStart + bufferStart + prefix,
                selectedWidth);
            edit.replacement.append(
                buffer.text(),
                lineStart + bufferEnd - suffix,
                suffix);
            selectedCells += selectedWidth;
        };

    std::size_t nextBufferColumn = 0;
    std::size_t nextDisplayColumn = 0;
    for (const DisplayCell &cell :
         layout.specialCells) {
        processLinearRun(
            nextBufferColumn,
            cell.bufferStart,
            nextDisplayColumn);
        const std::u16string_view grapheme(
            buffer.text().data()
                + lineStart + cell.bufferStart,
            cell.bufferEnd - cell.bufferStart);
        if (cell.displayEnd <= firstDisplayColumn
            || cell.displayStart
                >= lastDisplayColumnExclusive) {
            if (!insertionResolved
                && cell.displayStart
                    >= firstDisplayColumn) {
                edit.insertionColumn =
                    edit.replacement.size();
                insertionResolved = true;
            }
            edit.replacement.append(grapheme);
            continue;
        }

        const std::size_t selectedStart = std::max(
            firstDisplayColumn, cell.displayStart);
        const std::size_t selectedEnd = std::min(
            lastDisplayColumnExclusive,
            cell.displayEnd);
        if (cell.tab) {
            const std::size_t before =
                selectedStart - cell.displayStart;
            const std::size_t selectedWidth =
                selectedEnd - selectedStart;
            const std::size_t after =
                cell.displayEnd - selectedEnd;
            edit.replacement.append(before, u' ');
            if (!insertionResolved) {
                edit.insertionColumn =
                    edit.replacement.size();
                insertionResolved = true;
            }
            edit.selected.append(selectedWidth, u' ');
            edit.replacement.append(after, u' ');
            selectedCells += selectedWidth;
        } else {
            const std::size_t cellWidth =
                cell.displayEnd - cell.displayStart;
            const std::size_t selectedWidth =
                selectedEnd - selectedStart;
            if (selectedWidth == cellWidth) {
                if (!insertionResolved) {
                    edit.insertionColumn =
                        edit.replacement.size();
                    insertionResolved = true;
                }
                edit.selected.append(grapheme);
                selectedCells += cellWidth;
            } else {
                // A double-width grapheme cannot be split in the buffer.
                // Neovim's block_prep materializes both the selected and
                // retained halves as cells, preserving screen geometry.
                const std::size_t before =
                    selectedStart - cell.displayStart;
                const std::size_t after =
                    cell.displayEnd - selectedEnd;
                edit.replacement.append(before, u' ');
                if (!insertionResolved) {
                    edit.insertionColumn =
                        edit.replacement.size();
                    insertionResolved = true;
                }
                edit.selected.append(selectedWidth, u' ');
                edit.replacement.append(after, u' ');
                selectedCells += selectedWidth;
            }
        }
        nextBufferColumn = cell.bufferEnd;
        nextDisplayColumn = cell.displayEnd;
    }
    processLinearRun(
        nextBufferColumn,
        length,
        nextDisplayColumn);

    if (!insertionResolved) {
        edit.insertionColumn = edit.replacement.size();
    }
    if (selectedCells < requestedWidth) {
        // Block registers are rectangular: short lines contribute spaces,
        // while a delete still leaves their buffer text untouched.
        edit.selected.append(
            requestedWidth - selectedCells, u' ');
    }
    return edit;
}

[[nodiscard]] bool VkCore::Implementation::beginVisualBlockInsert(
    DispatchResult &result,
    const WindowId windowId,
    const bool append)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || !ownsVisualBlock(
            windowId, foundView->second.buffer)) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return true;
    }
    Buffer &buffer = foundBuffer->second;
    const auto range = currentVisualBlockRange(
        windowId, view, buffer);
    if (!range) {
        return true;
    }

    const std::size_t displayColumn = append
        ? range->lastColumnExclusive
        : range->firstColumn;
    const auto insertion = blockInsertionEdit(
        buffer,
        range->firstLine,
        displayColumn,
        true);
    if (!insertion) {
        return true;
    }
    const Mode before = effectiveMode();
    const BufferId bufferId = view.buffer;
    beginInsertUndoBlock(bufferId);
    baseMode = Mode::Insert;
    const bool normalized =
        insertion->bufferStart != insertion->bufferEnd
        || !insertion->replacement.empty();
    if (normalized) {
        const std::size_t lineStart =
            buffer.lineStarts[range->firstLine];
        if (!mutateBuffer(
                &result,
                bufferId,
                lineStart + insertion->bufferStart,
                lineStart + insertion->bufferEnd,
                insertion->replacement,
                std::pair<WindowId, Cursor>{
                    windowId,
                    Cursor{
                        range->firstLine,
                        insertion->resultingColumn}})) {
            baseMode = Mode::Visual;
            closeInsertUndoBlock();
            return true;
        }
    } else {
        view.cursors[bufferId] = Cursor{
            range->firstLine,
            insertion->resultingColumn};
        view.displayColumns[bufferId] = displayColumn;
        view.preferredColumn.reset();
        emitCursor(result, windowId, view);
    }
    leaveVisualMode();

    RepeatChange repeat;
    repeat.form = RepeatForm::VisualBlockInsert;
    repeat.command = append
        ? Command::AppendAtLineEnd
        : Command::InsertAtLineStart;
    repeat.blockHeight =
        range->lastLine - range->firstLine + 1;
    repeat.blockWidth =
        range->lastColumnExclusive
        - range->firstColumn;
    repeat.count = 1;
    beginInsertRepeat(
        repeat,
        offset(
            buffers.at(bufferId),
            views.at(windowId).cursors[bufferId]),
        normalized);
    activeBlockInsert = BlockInsertState{
        windowId,
        bufferId,
        range->firstLine,
        range->lastLine,
        displayColumn,
        append};
    emitModeIfChanged(result, before);
    return true;
}

[[nodiscard]] bool VkCore::Implementation::swapVisualBlockCorner(
    DispatchResult &result,
    const WindowId windowId,
    const bool horizontalOnly)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || !ownsVisualBlock(
            windowId, foundView->second.buffer)) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return true;
    }
    const DisplayPosition cursor =
        currentDisplayPosition(
            view, foundBuffer->second);
    const DisplayPosition anchor{
        visualSelection->anchor,
        visualSelection->anchorDisplayColumn};
    if (!horizontalOnly) {
        visualSelection->anchor = cursor.buffer;
        visualSelection->anchorDisplayColumn =
            cursor.column;
        setDisplayPosition(view, anchor);
        view.preferredColumn = anchor.column;
    } else {
        const auto boundedColumn =
            [this, &foundBuffer](
                const std::size_t line,
                const std::size_t column) {
                return virtualEdit
                        == VirtualEditMode::Block
                    ? column
                    : std::min(
                          column,
                          displayLine(
                              foundBuffer->second,
                              line)
                              .width);
            };
        const std::size_t cursorColumn =
            boundedColumn(
                cursor.buffer.line, anchor.column);
        const std::size_t anchorColumn =
            boundedColumn(
                anchor.buffer.line, cursor.column);
        const DisplayPosition newCursor =
            displayPositionForColumn(
                foundBuffer->second,
                cursor.buffer.line,
                cursorColumn);
        const DisplayPosition newAnchor =
            displayPositionForColumn(
                foundBuffer->second,
                anchor.buffer.line,
                anchorColumn);
        visualSelection->anchor = newAnchor.buffer;
        visualSelection->anchorDisplayColumn =
            newAnchor.column;
        setDisplayPosition(view, newCursor);
        view.preferredColumn = newCursor.column;
    }
    emitCursor(result, windowId, view);
    return true;
}

[[nodiscard]] std::vector<std::u16string_view>
VkCore::Implementation::blockRegisterRows(const std::u16string &text)
{
    std::vector<std::u16string_view> rows;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t separator = text.find(u'\n', start);
        if (separator == std::u16string::npos) {
            rows.emplace_back(
                text.data() + start,
                text.size() - start);
            break;
        }
        rows.emplace_back(
            text.data() + start,
            separator - start);
        start = separator + 1;
        if (start == text.size()) {
            break;
        }
    }
    return rows;
}

[[nodiscard]] bool VkCore::Implementation::putOverVisualBlock(
    DispatchResult &result,
    const WindowId windowId,
    const RegisterValue &payload,
    const Command command)
{
    const auto foundView = views.find(windowId);
    if (payload.empty()
        || foundView == views.end()
        || !ownsVisualBlock(
            windowId, foundView->second.buffer)) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return true;
    }
    Buffer &buffer = foundBuffer->second;
    const auto range = currentVisualBlockRange(
        windowId, view, buffer);
    if (!range) {
        return true;
    }
    const Mode before = effectiveMode();
    const std::size_t selectedHeight =
        range->lastLine - range->firstLine + 1;
    std::vector<std::u16string_view> payloadRows =
        payload.blockwise || payload.linewise
        ? blockRegisterRows(payload.text)
        : std::vector<std::u16string_view>{
              std::u16string_view(payload.text)};
    const std::size_t outputHeight = payload.blockwise
        ? std::max(selectedHeight, payloadRows.size())
        : selectedHeight;
    const std::size_t regionStart =
        buffer.lineStarts[range->firstLine];
    const std::size_t regionEnd =
        buffer.lineStarts[range->lastLine]
        + lineLength(buffer, range->lastLine);
    std::u16string replacement;
    std::u16string overwritten;
    std::size_t firstInsertionColumn = 0;
    for (std::size_t row = 0; row < outputHeight; ++row) {
        const std::size_t line = range->firstLine + row;
        std::u16string output;
        std::size_t insertionColumn = 0;
        if (row < selectedHeight) {
            BlockRowEdit edit = blockRowEdit(
                buffer,
                line,
                range->firstColumn,
                range->lastColumnExclusive);
            output = std::move(edit.replacement);
            insertionColumn = edit.insertionColumn;
            overwritten += edit.selected;
            if (row + 1 != selectedHeight) {
                overwritten.push_back(u'\n');
            }
            const std::size_t width =
                displayLine(buffer, line).width;
            if (range->firstColumn > width) {
                const std::size_t padding =
                    range->firstColumn - width;
                output.append(padding, u' ');
                insertionColumn = output.size();
            }
        } else {
            output.assign(range->firstColumn, u' ');
            insertionColumn = output.size();
        }

        std::u16string_view inserted;
        if (payload.blockwise) {
            if (row < payloadRows.size()) {
                inserted = payloadRows[row];
            }
        } else if (!payloadRows.empty()) {
            inserted = payloadRows.front();
        }
        output.insert(insertionColumn, inserted);
        if (row == 0) {
            firstInsertionColumn = insertionColumn;
        }
        replacement += output;
        if (row + 1 != outputHeight) {
            replacement.push_back(u'\n');
        }
    }
    // Visual put replaces the selection and writes the displaced block to
    // the delete registers, just like Neovim's block_put(). The payload
    // was copied before this side effect.
    writeOperationRegister(
        OperatorKind::Delete,
        std::move(overwritten),
        false,
        true,
        range->lastColumnExclusive
            - range->firstColumn);
    if (!mutateBuffer(
            &result,
            view.buffer,
            regionStart,
            regionEnd,
            std::move(replacement),
            std::pair<WindowId, Cursor>{
                windowId,
                Cursor{
                    range->firstLine,
                    firstInsertionColumn}})) {
        return true;
    }
    leaveVisualMode();
    view.preferredColumn.reset();
    RepeatChange repeat;
    repeat.form = RepeatForm::VisualBlockPut;
    repeat.command = command;
    repeat.put = payload;
    repeat.count = 1;
    repeat.blockHeight = selectedHeight;
    repeat.blockWidth =
        range->lastColumnExclusive
        - range->firstColumn;
    rememberChange(std::move(repeat));
    emitModeIfChanged(result, before);
    emitCursor(result, windowId, view);
    return true;
}

[[nodiscard]] bool VkCore::Implementation::replaceVisualBlock(
    DispatchResult &result,
    const WindowId windowId,
    const char32_t replacementCharacter,
    const bool remember)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || !ownsVisualBlock(
            windowId, foundView->second.buffer)) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return true;
    }
    Buffer &buffer = foundBuffer->second;
    const auto range = currentVisualBlockRange(
        windowId, view, buffer);
    if (!range) {
        return true;
    }
    std::u16string glyph;
    appendUtf16(glyph, replacementCharacter);
    if (glyph.empty()) {
        return true;
    }
    const std::size_t glyphWidth =
        graphemeCellWidth(glyph);
    const std::size_t requestedWidth =
        range->lastColumnExclusive
        - range->firstColumn;
    std::u16string fill;
    std::size_t filled = 0;
    while (filled + glyphWidth <= requestedWidth) {
        fill += glyph;
        filled += glyphWidth;
    }
    fill.append(requestedWidth - filled, u' ');

    const Mode before = effectiveMode();
    const std::size_t regionStart =
        buffer.lineStarts[range->firstLine];
    const std::size_t regionEnd =
        buffer.lineStarts[range->lastLine]
        + lineLength(buffer, range->lastLine);
    std::u16string replacement;
    std::size_t firstInsertionColumn = 0;
    for (std::size_t line = range->firstLine;
         line <= range->lastLine;
         ++line) {
        BlockRowEdit row = blockRowEdit(
            buffer,
            line,
            range->firstColumn,
            range->lastColumnExclusive);
        const std::size_t width =
            displayLine(buffer, line).width;
        if (range->firstColumn > width) {
            row.replacement.append(
                range->firstColumn - width,
                u' ');
            row.insertionColumn = row.replacement.size();
        }
        if (line == range->firstLine) {
            firstInsertionColumn = row.insertionColumn;
        }
        row.replacement.insert(
            row.insertionColumn, fill);
        replacement += row.replacement;
        if (line != range->lastLine) {
            replacement.push_back(u'\n');
        }
    }
    if (!mutateBuffer(
            &result,
            view.buffer,
            regionStart,
            regionEnd,
            std::move(replacement),
            std::pair<WindowId, Cursor>{
                windowId,
                Cursor{
                    range->firstLine,
                    firstInsertionColumn}})) {
        return true;
    }
    leaveVisualMode();
    view.preferredColumn.reset();
    if (remember) {
        RepeatChange repeat;
        repeat.form = RepeatForm::VisualBlockReplace;
        repeat.command = Command::BeginReplace;
        repeat.argument = replacementCharacter;
        repeat.blockHeight =
            range->lastLine - range->firstLine + 1;
        repeat.blockWidth = requestedWidth;
        rememberChange(std::move(repeat));
    }
    emitModeIfChanged(result, before);
    emitCursor(result, windowId, view);
    return true;
}

[[nodiscard]] bool VkCore::Implementation::toggleVisualBlock(
    DispatchResult &result,
    const WindowId windowId,
    const bool remember)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || !ownsVisualBlock(
            windowId, foundView->second.buffer)) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return true;
    }
    Buffer &buffer = foundBuffer->second;
    const auto range = currentVisualBlockRange(
        windowId, view, buffer);
    if (!range) {
        return true;
    }
    const Mode before = effectiveMode();
    const std::size_t regionStart =
        buffer.lineStarts[range->firstLine];
    const std::size_t regionEnd =
        buffer.lineStarts[range->lastLine]
        + lineLength(buffer, range->lastLine);
    std::u16string replacement;
    for (std::size_t line = range->firstLine;
         line <= range->lastLine;
         ++line) {
        const std::size_t length = lineLength(buffer, line);
        const VisualBlockRow row = visualBlockRow(
            buffer, line, *range);
        std::u16string output;
        output.append(
            buffer.text(),
            buffer.lineStarts[line],
            row.selectedBufferStart);
        std::size_t column = row.selectedBufferStart;
        while (column < row.selectedBufferEnd) {
            const std::size_t next = nextColumnAllowEnd(
                buffer, line, column);
            const QString scalar =
                QString::fromStdU16String(
                    buffer.text().substr(
                        buffer.lineStarts[line] + column,
                        next - column));
            const QString upper = scalar.toUpper();
            const QString lower = scalar.toLower();
            output += (scalar == upper ? lower : upper)
                          .toStdU16String();
            column = next;
        }
        output.append(
            buffer.text(),
            buffer.lineStarts[line]
                + row.selectedBufferEnd,
            length - row.selectedBufferEnd);
        replacement += output;
        if (line != range->lastLine) {
            replacement.push_back(u'\n');
        }
    }
    const Cursor target = normalCursorForDisplayColumn(
        buffer,
        range->firstLine,
        range->firstColumn);
    if (!mutateBuffer(
            &result,
            view.buffer,
            regionStart,
            regionEnd,
            std::move(replacement),
            std::pair<WindowId, Cursor>{windowId, target})) {
        return true;
    }
    leaveVisualMode();
    view.preferredColumn.reset();
    if (remember) {
        RepeatChange repeat;
        repeat.form = RepeatForm::VisualBlockToggle;
        repeat.command = Command::ToggleCase;
        repeat.blockHeight =
            range->lastLine - range->firstLine + 1;
        repeat.blockWidth =
            range->lastColumnExclusive
            - range->firstColumn;
        rememberChange(std::move(repeat));
    }
    emitModeIfChanged(result, before);
    emitCursor(result, windowId, view);
    return true;
}

[[nodiscard]] bool VkCore::Implementation::finishVisualTransform(
    DispatchResult &result,
    const WindowId windowId,
    const OperatorKind operation,
    const std::size_t rawCount,
    const bool remember)
{
    const auto foundView = views.find(windowId);
    if (!visualSelection
        || foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0
        || visualSelection->window != windowId
        || visualSelection->buffer
            != foundView->second.buffer) {
        (void)cancelStaleVisual(result, windowId);
        return false;
    }
    View &view = foundView->second;
    auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor anchor = clampCursor(
        buffer, visualSelection->anchor, false);
    const Cursor cursor = clampCursor(
        buffer, view.cursors[view.buffer], false);
    const VisualKind kind = visualSelection->kind;
    const std::size_t count =
        std::max<std::size_t>(1, rawCount);
    const std::size_t firstLine =
        std::min(anchor.line, cursor.line);
    const std::size_t lastLine =
        std::max(anchor.line, cursor.line);
    std::size_t start = 0;
    std::size_t end = 0;
    Cursor target;
    std::u16string replacement;
    std::size_t blockWidth = 1;

    if (operation == OperatorKind::ShiftRight
        || operation == OperatorKind::ShiftLeft) {
        if (kind != VisualKind::Block) {
            start = buffer.lineStarts[firstLine];
            end = buffer.lineStarts[lastLine]
                + lineLength(buffer, lastLine);
            replacement = shiftedLines(
                buffer,
                firstLine,
                lastLine,
                operation == OperatorKind::ShiftRight,
                count);
            const Cursor first =
                offset(buffer, anchor)
                        <= offset(buffer, cursor)
                    ? anchor
                    : cursor;
            target = kind == VisualKind::Line
                ? Cursor{firstLine, 0}
                : Cursor{firstLine, first.column};
        } else {
            const auto range = currentVisualBlockRange(
                windowId, view, buffer);
            if (!range) {
                return false;
            }
            start = buffer.lineStarts[range->firstLine];
            end = buffer.lineStarts[range->lastLine]
                + lineLength(buffer, range->lastLine);
            const std::size_t shift =
                std::max<std::size_t>(1, buffer.tabStop);
            const std::size_t cells = count
                    > std::numeric_limits<std::size_t>::max()
                        / shift
                ? std::numeric_limits<std::size_t>::max()
                : count * shift;
            std::size_t firstInsertion = 0;
            for (std::size_t line = range->firstLine;
                 line <= range->lastLine;
                 ++line) {
                const std::size_t length =
                    lineLength(buffer, line);
                const BlockInsertionEdit insertion =
                    blockInsertionEdit(
                        buffer,
                        line,
                        range->firstColumn,
                        true)
                        .value_or(BlockInsertionEdit{});
                std::u16string output;
                output.append(
                    buffer.text(),
                    buffer.lineStarts[line],
                    insertion.bufferStart);
                output += insertion.replacement;
                output.append(
                    buffer.text(),
                    buffer.lineStarts[line]
                        + insertion.bufferEnd,
                    length - insertion.bufferEnd);
                const std::size_t at =
                    insertion.bufferStart
                    + insertion.insertionOffset;
                if (line == range->firstLine) {
                    firstInsertion = at;
                }
                if (operation
                    == OperatorKind::ShiftRight) {
                    output.insert(at, cells, u' ');
                } else {
                    std::size_t remaining = cells;
                    std::size_t column = at;
                    std::size_t display =
                        range->firstColumn;
                    while (remaining != 0
                           && column < output.size()) {
                        if (output[column] == u' ') {
                            output.erase(column, 1);
                            --remaining;
                            ++display;
                            continue;
                        }
                        if (output[column] != u'\t') {
                            break;
                        }
                        const std::size_t tabWidth = shift
                            - display % shift;
                        if (tabWidth <= remaining) {
                            output.erase(column, 1);
                            remaining -= tabWidth;
                            display += tabWidth;
                        } else {
                            output.replace(
                                column,
                                1,
                                tabWidth - remaining,
                                u' ');
                            remaining = 0;
                        }
                    }
                }
                replacement += output;
                if (line != range->lastLine) {
                    replacement.push_back(u'\n');
                }
            }
            target = Cursor{
                range->firstLine, firstInsertion};
            blockWidth = range->lastColumnExclusive
                - range->firstColumn;
        }
    } else if (kind == VisualKind::Block) {
        const auto range = currentVisualBlockRange(
            windowId, view, buffer);
        if (!range) {
            return false;
        }
        start = buffer.lineStarts[range->firstLine];
        end = buffer.lineStarts[range->lastLine]
            + lineLength(buffer, range->lastLine);
        std::size_t firstColumn = 0;
        for (std::size_t line = range->firstLine;
             line <= range->lastLine;
             ++line) {
            const std::size_t length =
                lineLength(buffer, line);
            const VisualBlockRow row = visualBlockRow(
                buffer, line, *range);
            std::u16string output;
            output.append(
                buffer.text(),
                buffer.lineStarts[line],
                row.selectedBufferStart);
            const QString selected = QString::fromStdU16String(
                buffer.text().substr(
                    buffer.lineStarts[line]
                        + row.selectedBufferStart,
                    row.selectedBufferEnd
                        - row.selectedBufferStart));
            output += (operation == OperatorKind::Lowercase
                           ? selected.toLower()
                           : selected.toUpper())
                          .toStdU16String();
            output.append(
                buffer.text(),
                buffer.lineStarts[line]
                    + row.selectedBufferEnd,
                length - row.selectedBufferEnd);
            if (line == range->firstLine) {
                firstColumn = row.selectedBufferStart;
            }
            replacement += output;
            if (line != range->lastLine) {
                replacement.push_back(u'\n');
            }
        }
        target = Cursor{range->firstLine, firstColumn};
        blockWidth = range->lastColumnExclusive
            - range->firstColumn;
    } else {
        if (kind == VisualKind::Line) {
            start = buffer.lineStarts[firstLine];
            end = buffer.lineStarts[lastLine]
                + lineLength(buffer, lastLine);
            target = Cursor{firstLine, 0};
        } else {
            const std::size_t anchorOffset =
                offset(buffer, anchor);
            const std::size_t cursorOffset =
                offset(buffer, cursor);
            const Cursor first = anchorOffset <= cursorOffset
                ? anchor
                : cursor;
            const Cursor last = anchorOffset <= cursorOffset
                ? cursor
                : anchor;
            start = std::min(anchorOffset, cursorOffset);
            end = cursorCharacterEnd(buffer, last);
            target = first;
            if (first.line == last.line) {
                blockWidth = 0;
                for (std::size_t at = start;
                     at < end;
                     at = nextScalarOffset(
                         buffer.text(), at)) {
                    ++blockWidth;
                }
            }
        }
        const QString selected = QString::fromUtf16(
            buffer.text().data() + start,
            static_cast<qsizetype>(end - start));
        replacement =
            (operation == OperatorKind::Lowercase
                 ? selected.toLower()
                 : selected.toUpper())
                .toStdU16String();
    }

    const Mode beforeMode = effectiveMode();
    const bool changed = end - start != replacement.size()
        || !std::equal(
            replacement.cbegin(),
            replacement.cend(),
            buffer.text().cbegin()
                + static_cast<std::ptrdiff_t>(start));
    if (changed
        && !mutateBuffer(
            &result,
            view.buffer,
            start,
            end,
            std::move(replacement),
            std::pair<WindowId, Cursor>{
                windowId, target})) {
        return false;
    }
    if (!changed) {
        view.cursors[view.buffer] = target;
        view.displayColumns.erase(view.buffer);
        view.preferredColumn.reset();
    }
    leaveVisualMode();
    if (remember) {
        RepeatChange repeat;
        repeat.form = RepeatForm::VisualTransform;
        repeat.operation = operation;
        repeat.count = count;
        repeat.visualKind = kind;
        repeat.blockHeight = lastLine - firstLine + 1;
        repeat.blockWidth = std::max<std::size_t>(
            1, blockWidth);
        repeat.visualEndColumn =
            std::max(anchor.column, cursor.column);
        repeat.preserveVisualShape = true;
        rememberChange(std::move(repeat));
    }
    emitModeIfChanged(result, beforeMode);
    emitCursor(result, windowId, view);
    return true;
}

void VkCore::Implementation::finishVisualOperation(
    DispatchResult &result,
    const WindowId windowId,
    const OperatorKind operation)
{
    const auto foundView = views.find(windowId);
    if (!visualSelection
        || foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0
        || visualSelection->window != windowId
        || visualSelection->buffer
            != foundView->second.buffer) {
        (void)cancelStaleVisual(result, windowId);
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    Buffer &buffer = foundBuffer->second;
    if (operation != OperatorKind::Yank
        && rejectReadOnly(
            result, windowId, buffer)) {
        return;
    }
    const Cursor anchor = clampCursor(
        buffer, visualSelection->anchor, false);
    const Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const Mode beforeMode = effectiveMode();
    const VisualKind visualKind =
        visualSelection->kind;
    const char32_t operationRegister = selectedRegister;
    const bool linewise =
        visualKind == VisualKind::Line;
    std::size_t firstLine =
        std::min(anchor.line, cursor.line);
    std::size_t lastLine =
        std::max(anchor.line, cursor.line);
    std::size_t start = 0;
    std::size_t end = 0;
    Cursor target;
    std::u16string selected;
    std::size_t repeatCount = 1;

    if (visualKind == VisualKind::Block) {
        const auto range = currentVisualBlockRange(
            windowId, view, buffer);
        if (!range) {
            return;
        }
        firstLine = range->firstLine;
        lastLine = range->lastLine;
        start = buffer.lineStarts[firstLine];
        end = buffer.lineStarts[lastLine]
            + lineLength(buffer, lastLine);
        std::u16string replacement;
        replacement.reserve(end - start);
        std::size_t firstInsertionColumn = 0;
        for (std::size_t line = firstLine;
             line <= lastLine;
             ++line) {
            BlockRowEdit row = blockRowEdit(
                buffer,
                line,
                range->firstColumn,
                range->lastColumnExclusive);
            if (line == firstLine) {
                firstInsertionColumn =
                    row.insertionColumn;
            }
            selected += row.selected;
            replacement += row.replacement;
            if (line != lastLine) {
                selected.push_back(u'\n');
                replacement.push_back(u'\n');
            }
        }
        target = Cursor{firstLine, firstInsertionColumn};
        repeatCount = lastLine - firstLine + 1;
        writeOperationRegister(
            operation,
            selected,
            false,
            true,
            range->lastColumnExclusive
                - range->firstColumn);
        if (operation == OperatorKind::Yank) {
            leaveVisualMode();
            const Cursor destination =
                normalCursorForDisplayColumn(
                    buffer,
                    firstLine,
                    range->firstColumn);
            view.cursors[view.buffer] = destination;
            view.displayColumns[view.buffer] =
                displayColumnForBufferColumn(
                    buffer,
                    destination.line,
                    destination.column);
            view.preferredColumn.reset();
            emitModeIfChanged(result, beforeMode);
            emitCursor(result, windowId, view);
            return;
        }

        if (operation == OperatorKind::Change) {
            beginInsertUndoBlock(view.buffer);
            baseMode = Mode::Insert;
        }
        if (!mutateBuffer(
                &result,
                view.buffer,
                start,
                end,
                std::move(replacement),
                std::pair<WindowId, Cursor>{
                    windowId, target})) {
            if (operation == OperatorKind::Change) {
                baseMode = Mode::Visual;
                closeInsertUndoBlock();
            }
            return;
        }
        leaveVisualMode();
        RepeatChange repeat;
        repeat.form = RepeatForm::VisualBlock;
        repeat.operation = operation;
        repeat.count = 1;
        repeat.blockHeight = repeatCount;
        repeat.blockWidth =
            range->lastColumnExclusive
                - range->firstColumn;
        repeat.command = Command::SubstituteCharacters;
        repeat.registerName = operationRegister;
        if (operation == OperatorKind::Change) {
            const Cursor insertionCursor =
                views.at(windowId)
                    .cursors[view.buffer];
            beginInsertRepeat(
                repeat,
                offset(
                    buffers.at(view.buffer),
                    insertionCursor),
                true);
            activeBlockInsert = BlockInsertState{
                windowId,
                view.buffer,
                firstLine,
                lastLine,
                range->firstColumn};
        } else {
            rememberChange(std::move(repeat));
        }
        emitModeIfChanged(result, beforeMode);
        return;
    }

    if (linewise) {
        start = buffer.lineStarts[firstLine];
        end = lastLine + 1 < buffer.lineStarts.size()
            ? buffer.lineStarts[lastLine + 1]
            : buffer.text().size();
        selected = linewiseText(
            buffer, firstLine, lastLine);
        repeatCount = lastLine - firstLine + 1;
        target = Cursor{firstLine, 0};
        if (operation == OperatorKind::Delete
            && end == buffer.text().size()
            && start > 0) {
            --start;
            target.line = firstLine > 0
                ? firstLine - 1
                : 0;
        } else if (
            operation == OperatorKind::Change) {
            end = buffer.lineStarts[lastLine]
                + lineLength(buffer, lastLine);
        }
    } else {
        const std::size_t anchorOffset =
            offset(buffer, anchor);
        const std::size_t cursorOffset =
            offset(buffer, cursor);
        const Cursor first =
            anchorOffset <= cursorOffset
            ? anchor
            : cursor;
        const Cursor last =
            anchorOffset <= cursorOffset
            ? cursor
            : anchor;
        start = std::min(
            anchorOffset, cursorOffset);
        end = cursorCharacterEnd(buffer, last);
        selected = buffer.text().substr(
            start, end - start);
        target = first;
        if (first.line == last.line) {
            std::size_t column = first.column;
            repeatCount = 0;
            while (column
                   < nextColumnAllowEnd(
                       buffer,
                       last.line,
                       last.column)) {
                column = nextColumnAllowEnd(
                    buffer, first.line, column);
                ++repeatCount;
            }
        }
    }

    writeOperationRegister(
        operation, selected, linewise);
    if (operation == OperatorKind::Yank) {
        leaveVisualMode();
        view.cursors[view.buffer] = target;
        view.preferredColumn.reset();
        emitModeIfChanged(result, beforeMode);
        emitCursor(result, windowId, view);
        return;
    }

    if (operation == OperatorKind::Change) {
        beginInsertUndoBlock(view.buffer);
        baseMode = Mode::Insert;
    }
    if (!mutateBuffer(
            &result,
            view.buffer,
            start,
            end,
            {},
            std::pair<WindowId, Cursor>{
                windowId, target})) {
        if (operation == OperatorKind::Change) {
            baseMode = Mode::Visual;
            closeInsertUndoBlock();
        }
        return;
    }

    leaveVisualMode();
    RepeatChange repeat;
    repeat.count =
        std::max<std::size_t>(1, repeatCount);
    repeat.registerName = operationRegister;
    repeat.command = linewise
        ? Command::SubstituteLines
        : Command::SubstituteCharacters;
    if (linewise) {
        // Dot after a Visual operation reuses the remembered Visual
        // shape; an explicit count before dot does not replace it.
        repeat.preserveVisualShape = true;
    } else {
        const Cursor first =
            offset(buffer, anchor) <= offset(buffer, cursor)
            ? anchor
            : cursor;
        const Cursor last = first == anchor
            ? cursor
            : anchor;
        repeat.form = RepeatForm::VisualCharacter;
        repeat.operation = operation;
        repeat.blockHeight =
            last.line - first.line + 1;
        repeat.blockWidth =
            std::max<std::size_t>(1, repeatCount);
        repeat.visualEndColumn = last.column;
        repeat.preserveVisualShape = true;
    }
    if (operation == OperatorKind::Change) {
        const Cursor insertionCursor =
            views.at(windowId)
                .cursors[view.buffer];
        beginInsertRepeat(
            repeat,
            offset(buffer, insertionCursor),
            true);
    } else {
        if (linewise) {
            repeat.form =
                RepeatForm::OperatorLinewise;
            repeat.operation =
                OperatorKind::Delete;
        }
        rememberChange(std::move(repeat));
    }
    emitModeIfChanged(result, beforeMode);
}

} // namespace vkui::vk
