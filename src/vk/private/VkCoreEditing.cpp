#include "VkCoreInternal.h"

namespace vkui::vk {

[[nodiscard]] bool VkCore::Implementation::rejectReadOnly(
    DispatchResult &result,
    const WindowId windowId,
    const Buffer &buffer) const
{
    if (!buffer.readOnly) {
        return false;
    }
    Event error;
    error.type = EventType::InputError;
    error.view = windowId;
    error.buffer = buffer.id;
    error.message = "buffer is read-only";
    result.events.push_back(std::move(error));
    return true;
}

[[nodiscard]] bool VkCore::Implementation::deleteCharacters(
    DispatchResult &result,
    const WindowId windowId,
    const std::size_t count,
    const bool beforeCursor,
    const bool enterInsert,
    const Command repeatCommand)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const std::size_t length =
        lineLength(buffer, cursor.line);
    std::size_t startColumn = cursor.column;
    std::size_t endColumn = cursor.column;
    if (beforeCursor) {
        startColumn = retreatWithinLine(
            buffer,
            cursor.line,
            cursor.column,
            count);
    } else {
        if (length != 0) {
            endColumn = advanceWithinLine(
                buffer,
                cursor.line,
                cursor.column,
                count);
        }
    }
    const std::size_t lineStart =
        buffer.lineStarts[cursor.line];
    const std::size_t start =
        lineStart + startColumn;
    const std::size_t end =
        lineStart + endColumn;
    const bool changed = start != end;
    const char32_t operationRegister = selectedRegister;
    const Mode beforeMode = effectiveMode();
    if (enterInsert) {
        beginInsertUndoBlock(view.buffer);
        baseMode = Mode::Insert;
    }
    if (changed) {
        const std::u16string removed =
            buffer.text().substr(
                start, end - start);
        if (!mutateBuffer(
                &result,
                view.buffer,
                start,
                end,
                {},
                std::pair<WindowId, Cursor>{
                    windowId,
                    Cursor{
                        cursor.line,
                        startColumn}})) {
            if (enterInsert) {
                baseMode = beforeMode;
                closeInsertUndoBlock();
            }
            return false;
        }
        writeDeleteRegister(
            removed, false);
    }

    RepeatChange repeat;
    repeat.command = repeatCommand;
    repeat.count = count;
    repeat.registerName = operationRegister;
    if (enterInsert) {
        pendingOperator.reset();
        prefix = CommandPrefix::None;
        const Cursor insertionCursor =
            views.at(windowId)
                .cursors[view.buffer];
        beginInsertRepeat(
            repeat,
            offset(buffer, insertionCursor),
            changed);
        emitModeIfChanged(result, beforeMode);
    } else if (changed) {
        rememberChange(std::move(repeat));
    }
    return changed || enterInsert;
}

[[nodiscard]] bool VkCore::Implementation::changeToLineEnd(
    DispatchResult &result,
    const WindowId windowId,
    const bool enterInsert,
    const std::size_t count,
    const Command repeatCommand)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const std::size_t start =
        offset(buffer, cursor);
    const std::size_t lastLine = std::min(
        buffer.lineStarts.size() - 1,
        cursor.line + count - 1);
    const std::size_t end =
        buffer.lineStarts[lastLine]
        + lineLength(buffer, lastLine);
    const bool changed = start != end;
    const char32_t operationRegister = selectedRegister;
    const Mode beforeMode = effectiveMode();
    if (enterInsert) {
        beginInsertUndoBlock(view.buffer);
        baseMode = Mode::Insert;
    }
    if (changed) {
        const std::u16string removed =
            buffer.text().substr(
                start, end - start);
        if (!mutateBuffer(
                &result,
                view.buffer,
                start,
                end,
                {},
                std::pair<WindowId, Cursor>{
                    windowId, cursor})) {
            if (enterInsert) {
                baseMode = beforeMode;
                closeInsertUndoBlock();
            }
            return false;
        }
        writeDeleteRegister(
            removed,
            false);
    }

    RepeatChange repeat;
    repeat.command = repeatCommand;
    repeat.count = count;
    repeat.registerName = operationRegister;
    if (enterInsert) {
        pendingOperator.reset();
        prefix = CommandPrefix::None;
        beginInsertRepeat(
            repeat,
            start,
            changed);
        emitModeIfChanged(result, beforeMode);
    } else if (changed) {
        rememberChange(std::move(repeat));
    }
    return changed || enterInsert;
}

[[nodiscard]] bool VkCore::Implementation::substituteLines(
    DispatchResult &result,
    const WindowId windowId,
    const std::size_t count,
    const Command repeatCommand)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const std::size_t lastLine = std::min(
        buffer.lineStarts.size() - 1,
        cursor.line + count - 1);
    const std::size_t start =
        buffer.lineStarts[cursor.line];
    const std::size_t end =
        buffer.lineStarts[lastLine]
        + lineLength(buffer, lastLine);
    std::u16string removed =
        buffer.text().substr(
            start, end - start);
    const char32_t operationRegister = selectedRegister;
    if (!removed.empty()
        && removed.back() != u'\n') {
        removed.push_back(u'\n');
    }

    const Mode beforeMode = effectiveMode();
    beginInsertUndoBlock(view.buffer);
    baseMode = Mode::Insert;
    const bool changed = start != end;
    if (changed
        && !mutateBuffer(
            &result,
            view.buffer,
            start,
            end,
            {},
            std::pair<WindowId, Cursor>{
                windowId,
                Cursor{cursor.line, 0}})) {
        closeInsertUndoBlock();
        baseMode = beforeMode;
        return false;
    }
    if (changed) {
        writeDeleteRegister(
            std::move(removed), true);
    }
    pendingOperator.reset();
    prefix = CommandPrefix::None;
    RepeatChange repeat;
    repeat.command = repeatCommand;
    repeat.count = count;
    repeat.registerName = operationRegister;
    beginInsertRepeat(
        repeat, start, changed);
    emitModeIfChanged(result, beforeMode);
    return true;
}

[[nodiscard]] bool VkCore::Implementation::openLine(
    DispatchResult &result,
    const WindowId windowId,
    const std::size_t count,
    const bool below,
    const Command repeatCommand)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const std::size_t insertion =
        below
        ? buffer.lineStarts[cursor.line]
            + lineLength(buffer, cursor.line)
        : buffer.lineStarts[cursor.line];
    // A count before o/O repeats the completed inserted line, not merely
    // the newline. Create one editable line now; finishInsertRepeat()
    // materializes the remaining copies after Insert mode is complete.
    std::u16string newlines(1, u'\n');
    const Cursor target{
        below
            ? cursor.line + 1
            : cursor.line,
        0};
    const Mode beforeMode = effectiveMode();
    beginInsertUndoBlock(view.buffer);
    baseMode = Mode::Insert;
    if (!mutateBuffer(
            &result,
            view.buffer,
            insertion,
            insertion,
            std::move(newlines),
            std::pair<WindowId, Cursor>{
                windowId, target})) {
        closeInsertUndoBlock();
        baseMode = beforeMode;
        return false;
    }
    pendingOperator.reset();
    prefix = CommandPrefix::None;
    RepeatChange repeat;
    repeat.command = repeatCommand;
    repeat.count = count;
    beginInsertRepeat(
        repeat,
        buffer.lineStarts[target.line],
        true);
    emitModeIfChanged(result, beforeMode);
    return true;
}

[[nodiscard]] bool VkCore::Implementation::replaceCharacters(
    DispatchResult &result,
    const WindowId windowId,
    const std::size_t count,
    const char32_t replacement,
    const bool remember)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const std::size_t startColumn = cursor.column;
    const std::size_t contentLength =
        lineLength(buffer, cursor.line);
    std::size_t endColumn = startColumn;
    std::size_t available = 0;
    while (endColumn < contentLength
           && available < count) {
        endColumn = nextColumnAllowEnd(
            buffer, cursor.line, endColumn);
        ++available;
    }
    // nv_replace() rejects the complete command when the requested
    // character count crosses EOL. A partial replacement would both
    // diverge from Neovim and create an incorrect dot-repeat recipe.
    if (available < count) {
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.buffer = view.buffer;
        error.message =
            "not enough characters to replace";
        result.events.push_back(std::move(error));
        return false;
    }
    std::u16string scalar;
    appendUtf16(scalar, replacement);
    std::u16string inserted;
    // Like Neovim, r<CR> removes [count] characters but inserts one
    // newline; ordinary replacement scalars are repeated [count] times.
    const std::size_t insertionCount =
        replacement == U'\n' ? 1 : count;
    inserted.reserve(
        scalar.size() * insertionCount);
    for (std::size_t index = 0;
         index < insertionCount;
         ++index) {
        inserted += scalar;
    }
    const std::size_t start =
        buffer.lineStarts[cursor.line]
        + startColumn;
    const std::size_t end =
        buffer.lineStarts[cursor.line]
        + endColumn;
    const std::size_t lastStart =
        inserted.size() >= scalar.size()
        ? inserted.size() - scalar.size()
        : 0;
    if (!mutateBuffer(
            &result,
            view.buffer,
            start,
            end,
            inserted,
            std::pair<WindowId, Cursor>{
                windowId,
                Cursor{
                    cursor.line,
                    startColumn + lastStart}})) {
        return false;
    }
    if (remember) {
        RepeatChange repeat;
        repeat.command = Command::BeginReplace;
        repeat.count = count;
        repeat.argument = replacement;
        rememberChange(std::move(repeat));
    }
    return true;
}

[[nodiscard]] bool VkCore::Implementation::put(
    DispatchResult &result,
    const WindowId windowId,
    const RegisterValue &payload,
    const std::size_t count,
    const bool after,
    const bool remember,
    const char32_t registerName)
{
    const auto foundView = views.find(windowId);
    if (payload.empty()
        || foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);

    if (payload.blockwise) {
        std::vector<std::u16string_view> rows;
        std::size_t rowStart = 0;
        while (rowStart <= payload.text.size()) {
            const std::size_t separator =
                payload.text.find(u'\n', rowStart);
            if (separator == std::u16string::npos) {
                rows.emplace_back(
                    payload.text.data() + rowStart,
                    payload.text.size() - rowStart);
                break;
            }
            rows.emplace_back(
                payload.text.data() + rowStart,
                separator - rowStart);
            rowStart = separator + 1;
            if (rowStart == payload.text.size()) {
                break;
            }
        }
        if (rows.empty()) {
            return false;
        }
        const DisplayPosition cursorDisplay =
            currentDisplayPosition(view, buffer);
        const auto cursorSpan = displaySpan(
            buffer, cursorDisplay, false);
        const std::size_t insertionDisplayColumn =
            after ? cursorSpan.second : cursorSpan.first;
        const std::size_t firstLine = cursor.line;
        const std::size_t existingLastLine =
            buffer.lineStarts.size() - 1;
        const std::size_t lastLine =
            firstLine + rows.size() - 1;
        const std::size_t regionStart =
            buffer.lineStarts[firstLine];
        const std::size_t regionEnd =
            buffer.lineStarts[existingLastLine]
            + lineLength(buffer, existingLastLine);
        std::u16string replacement;
        std::size_t firstInsertionColumn = 0;
        for (std::size_t row = 0;
             row < rows.size();
             ++row) {
            const std::size_t line = firstLine + row;
            std::u16string original;
            if (line <= existingLastLine) {
                original = buffer.text().substr(
                    buffer.lineStarts[line],
                    lineLength(buffer, line));
            }
            std::u16string repeated;
            if (!rows[row].empty()
                && count
                    <= std::numeric_limits<std::size_t>::max()
                        / rows[row].size()) {
                repeated.reserve(rows[row].size() * count);
            }
            for (std::size_t repetition = 0;
                 repetition < count;
                 ++repetition) {
                repeated.append(rows[row]);
            }
            std::size_t insertionColumn = original.size();
            if (line <= existingLastLine) {
                const DisplayLine &lineLayout =
                    displayLine(buffer, line);
                const DisplayBoundary boundary =
                    displayBoundary(
                        buffer,
                        line,
                        insertionDisplayColumn);
                if (boundary.cellBufferEnd
                        > boundary.bufferColumn
                    && insertionDisplayColumn
                        > boundary.cellDisplayStart) {
                    const std::size_t before =
                        insertionDisplayColumn
                            - boundary.cellDisplayStart;
                    const std::size_t afterCells =
                        boundary.cellDisplayEnd
                            - insertionDisplayColumn;
                    std::u16string expanded;
                    expanded.append(before, u' ');
                    expanded += repeated;
                    expanded.append(afterCells, u' ');
                    original.replace(
                        boundary.bufferColumn,
                        boundary.cellBufferEnd
                            - boundary.bufferColumn,
                        expanded);
                    insertionColumn =
                        boundary.bufferColumn + before;
                } else {
                    insertionColumn =
                        boundary.bufferColumn;
                    if (insertionDisplayColumn
                        > lineLayout.width) {
                        original.append(
                            insertionDisplayColumn
                                - lineLayout.width,
                            u' ');
                        insertionColumn = original.size();
                    }
                    original.insert(
                        insertionColumn, repeated);
                }
            } else {
                original.append(
                    insertionDisplayColumn, u' ');
                insertionColumn = original.size();
                original += repeated;
            }
            if (row == 0) {
                firstInsertionColumn = insertionColumn;
            }
            replacement += original;
            if (line != lastLine) {
                replacement.push_back(u'\n');
            }
        }
        const std::size_t replacedEnd =
            lastLine <= existingLastLine
            ? buffer.lineStarts[lastLine]
                + lineLength(buffer, lastLine)
            : regionEnd;
        if (!mutateBuffer(
                &result,
                view.buffer,
                regionStart,
                replacedEnd,
                replacement)) {
            return false;
        }
        View &updatedView = views.at(windowId);
        updatedView.cursors[view.buffer] =
            cursorAtOffset(
                buffer,
                regionStart + firstInsertionColumn);
        updatedView.displayColumns[view.buffer] =
            displayColumnForBufferColumn(
                buffer,
                updatedView.cursors[view.buffer].line,
                updatedView.cursors[view.buffer].column);
        updatedView.preferredColumn.reset();
        emitCursor(result, windowId, updatedView);
        if (remember) {
            RepeatChange repeat;
            repeat.command = after
                ? Command::PutAfter
                : Command::PutBefore;
            repeat.count = count;
            repeat.put = payload;
            repeat.registerName = registerName;
            rememberChange(std::move(repeat));
        }
        return true;
    }

    std::u16string inserted;
    if (payload.text.size() != 0
        && count
            <= std::numeric_limits<std::size_t>::max()
                / payload.text.size()) {
        inserted.reserve(
            payload.text.size() * count);
    }
    for (std::size_t index = 0;
         index < count;
         ++index) {
        inserted += payload.text;
    }

    std::size_t insertion = 0;
    std::size_t targetOffset = 0;
    if (payload.linewise) {
        if (inserted.empty()
            || inserted.back() != u'\n') {
            inserted.push_back(u'\n');
        }
        if (after) {
            if (cursor.line + 1
                < buffer.lineStarts.size()) {
                insertion =
                    buffer.lineStarts[cursor.line + 1];
                targetOffset = insertion;
            } else {
                insertion =
                    buffer.text().size();
                if (insertion != 0
                    && buffer.text().back()
                        != u'\n') {
                    inserted.insert(
                        inserted.cbegin(), u'\n');
                    targetOffset = insertion + 1;
                } else {
                    targetOffset = insertion;
                }
            }
        } else {
            insertion =
                buffer.lineStarts[cursor.line];
            targetOffset = insertion;
        }
    } else {
        const std::size_t lineLengthValue =
            lineLength(buffer, cursor.line);
        insertion = offset(buffer, cursor);
        if (after && lineLengthValue != 0) {
            insertion = cursorCharacterEnd(
                buffer, cursor);
        }
        targetOffset = after
            ? insertion + inserted.size() - 1
            : insertion;
    }

    if (!mutateBuffer(
            &result,
            view.buffer,
            insertion,
            insertion,
            inserted)) {
        return false;
    }
    View &updatedView = views.at(windowId);
    updatedView.cursors[view.buffer] =
        cursorAtOffset(buffer, targetOffset);
    updatedView.preferredColumn.reset();
    emitCursor(result, windowId, updatedView);

    if (remember) {
        RepeatChange repeat;
        repeat.command = after
            ? Command::PutAfter
            : Command::PutBefore;
        repeat.count = count;
        repeat.put = payload;
        repeat.registerName = registerName;
        rememberChange(std::move(repeat));
    }
    return true;
}

[[nodiscard]] bool VkCore::Implementation::joinLines(
    DispatchResult &result,
    const WindowId windowId,
    const std::size_t lineCount,
    const bool remember)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const std::size_t available =
        buffer.lineStarts.size() - cursor.line;
    const std::size_t actualLines =
        std::min(
            std::max<std::size_t>(2, lineCount),
            available);
    if (actualLines < 2) {
        return false;
    }
    const std::size_t start =
        buffer.lineStarts[cursor.line]
        + lineLength(buffer, cursor.line);
    const std::size_t lastLine =
        cursor.line + actualLines - 1;
    const std::size_t end =
        buffer.lineStarts[lastLine]
        + lineLength(buffer, lastLine);
    std::u16string joined;
    bool hasPreviousText =
        lineLength(buffer, cursor.line) != 0;
    char16_t previousCharacter =
        hasPreviousText
        ? buffer.text()[start - 1]
        : u'\0';
    for (std::size_t line = cursor.line + 1;
         line <= lastLine;
         ++line) {
        const std::size_t contentStart =
            buffer.lineStarts[line];
        const std::size_t contentLength =
            lineLength(buffer, line);
        std::size_t first = 0;
        while (first < contentLength
               && (buffer.text()[
                       contentStart + first]
                       == u' '
                   || buffer.text()[
                          contentStart + first]
                          == u'\t')) {
            ++first;
        }
        if (first == contentLength) {
            continue;
        }
        const char16_t nextCharacter =
            buffer.text()[
                contentStart + first];
        // Neovim's default ('nojoinspaces') preserves existing trailing
        // whitespace, suppresses a space before ')', and otherwise adds
        // exactly one space. Sentence punctuation does not add a second.
        if (hasPreviousText
            && previousCharacter != u' '
            && previousCharacter != u'\t'
            && nextCharacter != u')') {
            joined.push_back(u' ');
        }
        joined.append(buffer.text().substr(contentStart + first, contentLength - first));
        hasPreviousText = true;
        previousCharacter = joined.back();
    }
    if (!mutateBuffer(
            &result,
            view.buffer,
            start,
            end,
            joined,
            std::pair<WindowId, Cursor>{
                windowId,
                Cursor{
                    cursor.line,
                    lineLength(buffer, cursor.line)}})) {
        return false;
    }
    if (remember) {
        RepeatChange repeat;
        repeat.command = Command::JoinLines;
        repeat.count = actualLines;
        rememberChange(std::move(repeat));
    }
    return true;
}

[[nodiscard]] bool VkCore::Implementation::toggleCase(
    DispatchResult &result,
    const WindowId windowId,
    const std::size_t count,
    const bool remember)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer,
        view.cursors[view.buffer],
        false);
    const std::size_t startColumn = cursor.column;
    const std::size_t endColumn =
        advanceWithinLine(
            buffer,
            cursor.line,
            startColumn,
            count);
    if (startColumn == endColumn) {
        return false;
    }
    const std::size_t lineStart =
        buffer.lineStarts[cursor.line];
    std::u16string toggled;
    std::size_t column = startColumn;
    while (column < endColumn) {
        const std::size_t next =
            nextColumnAllowEnd(
                buffer, cursor.line, column);
        const QString scalar =
            QString::fromStdU16String(
                buffer.text().substr(
                    lineStart + column,
                    next - column));
        const QString upper = scalar.toUpper();
        const QString lower = scalar.toLower();
        toggled += (scalar == upper ? lower : upper)
                       .toStdU16String();
        column = next;
    }
    const std::size_t start =
        lineStart + startColumn;
    const std::size_t end =
        lineStart + endColumn;
    if (!mutateBuffer(
            &result,
            view.buffer,
            start,
            end,
            toggled,
            std::pair<WindowId, Cursor>{
                windowId,
                Cursor{
                    cursor.line,
                    startColumn + toggled.size()}})) {
        return false;
    }
    if (remember) {
        RepeatChange repeat;
        repeat.command = Command::ToggleCase;
        repeat.count = count;
        rememberChange(std::move(repeat));
    }
    return true;
}

} // namespace vkui::vk
