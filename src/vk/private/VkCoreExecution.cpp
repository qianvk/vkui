#include "VkCoreInternal.h"

namespace vkui::vk {

void VkCore::Implementation::execute(
    DispatchResult &result,
    const ViewId viewId,
    const Command command)
{
    const auto found = views.find(viewId);
    if (found == views.end()) {
        return;
    }
    View &view = found->second;
    const auto foundBuffer =
        buffers.find(view.buffer);
    const bool hasBuffer =
        view.buffer != 0
        && foundBuffer != buffers.end();
    const bool countWasExplicit = normalCount != 0;
    const std::size_t count =
        std::max<std::size_t>(1, normalCount);
    normalCount = 0;
    if (!verticalDisplayMotion(command)) {
        view.preferredDisplayRowColumn.reset();
    }

    switch (command) {
    case Command::MoveLeft:
    case Command::MoveDown:
    case Command::MoveUp:
    case Command::MoveRight:
        if (view.kind == ViewKind::Navigation
            || view.kind == ViewKind::Surface) {
            const HostAction action =
                command == Command::MoveLeft
                ? HostAction::NavigateLeft
                : command == Command::MoveDown
                ? HostAction::NavigateDown
                : command == Command::MoveUp
                ? HostAction::NavigateUp
                : HostAction::NavigateRight;
            for (std::size_t index = 0;
                 index < count;
                 ++index) {
                emitHost(result, action);
            }
        } else {
            moveCursor(
                result,
                view,
                viewId,
                command,
                count,
                countWasExplicit);
        }
        break;
    case Command::WordForward:
    case Command::WordBackward:
    case Command::WordEnd:
    case Command::WordEndBackward:
    case Command::BigWordForward:
    case Command::BigWordBackward:
    case Command::BigWordEnd:
    case Command::BigWordEndBackward:
    case Command::SentenceBackward:
    case Command::SentenceForward:
    case Command::ParagraphBackward:
    case Command::ParagraphForward:
    case Command::SectionBackwardStart:
    case Command::SectionForwardStart:
    case Command::SectionBackwardEnd:
    case Command::SectionForwardEnd:
    case Command::LineStart:
    case Command::FirstNonBlank:
    case Command::LineEnd:
    case Command::LastNonBlank:
    case Command::LineDownFirstNonBlank:
    case Command::LineUpFirstNonBlank:
    case Command::CurrentLineFirstNonBlank:
    case Command::ScreenColumn:
    case Command::FirstLine:
    case Command::LastLine:
    case Command::FilePercent:
    case Command::TextMiddle:
    case Command::WindowTop:
    case Command::WindowMiddle:
    case Command::WindowBottom:
        if (view.kind == ViewKind::Navigation
            || view.kind == ViewKind::Surface) {
            if (command == Command::FirstLine
                || command == Command::LastLine) {
                emitHost(
                    result,
                    command == Command::FirstLine
                        ? HostAction::NavigateFirst
                        : HostAction::NavigateLast,
                    count,
                    countWasExplicit);
            }
            return;
        }
        if (view.kind != ViewKind::Editor) {
            return;
        }
        moveCursor(
            result,
            view,
            viewId,
            command,
            count,
            countWasExplicit);
        break;
    case Command::MatchPair:
        if (view.kind == ViewKind::Editor) {
            if (countWasExplicit) {
                moveCursor(
                    result,
                    view,
                    viewId,
                    Command::FilePercent,
                    count,
                    true);
            } else {
                applyMatchingPairMotion(
                    result, viewId);
            }
        }
        break;
    case Command::DisplayRowUp:
    case Command::DisplayRowDown:
    case Command::DisplayRowStart:
    case Command::DisplayRowFirstNonBlank:
    case Command::DisplayRowEnd:
    case Command::DisplayRowMiddle:
        if (view.kind == ViewKind::Editor) {
            requestDisplayMotion(
                result,
                viewId,
                command,
                count,
                countWasExplicit);
        }
        break;
    case Command::BeginFindForward:
    case Command::BeginFindBackward:
    case Command::BeginTillForward:
    case Command::BeginTillBackward:
        beginCharacterSearchArgument(
            viewId,
            count,
            countWasExplicit,
            command == Command::BeginFindForward
                || command
                    == Command::BeginTillForward,
            command == Command::BeginTillForward
                || command
                    == Command::BeginTillBackward);
        break;
    case Command::RepeatCharacterSearch:
    case Command::RepeatCharacterSearchOpposite:
        applyRepeatedCharacterSearch(
            result,
            viewId,
            count,
            command
                == Command::
                    RepeatCharacterSearchOpposite);
        break;
    case Command::EnterInsert:
    case Command::InsertAtLastPosition:
    case Command::AppendInsert:
    case Command::InsertAtLineStart:
    case Command::AppendAtLineEnd: {
        if (view.kind != ViewKind::Editor
            || !hasBuffer) {
            return;
        }
        if (foundBuffer->second.readOnly) {
            Event error;
            error.type = EventType::InputError;
            error.view = viewId;
            error.buffer = view.buffer;
            error.message = "buffer is read-only";
            result.events.push_back(std::move(error));
            return;
        }
        if (command == Command::InsertAtLastPosition) {
            Cursor &cursor = view.cursors[view.buffer];
            cursor = foundBuffer->second.lastInsertExitMark
                ? cursorAtOffset(
                      foundBuffer->second,
                      *foundBuffer->second.lastInsertExitMark,
                      true)
                : Cursor{};
            view.displayColumns.erase(view.buffer);
            view.preferredColumn.reset();
            emitCursor(result, viewId, view);
        } else if (command == Command::AppendInsert) {
            const auto buffer = buffers.find(view.buffer);
            if (buffer != buffers.end()) {
                Cursor &cursor =
                    view.cursors[view.buffer];
                cursor.column = std::min(
                    lineLength(
                        buffer->second,
                        cursor.line),
                    cursor.column + 1);
                emitCursor(result, viewId, view);
            }
        } else if (
            command == Command::InsertAtLineStart) {
            const auto buffer =
                buffers.find(view.buffer);
            if (buffer != buffers.end()) {
                Cursor &cursor =
                    view.cursors[view.buffer];
                cursor.column =
                    firstNonBlankColumn(
                        buffer->second,
                        cursor.line,
                        true);
                view.preferredColumn.reset();
                emitCursor(
                    result, viewId, view);
            }
        } else if (
            command == Command::AppendAtLineEnd) {
            const auto buffer = buffers.find(view.buffer);
            if (buffer != buffers.end()) {
                Cursor &cursor =
                    view.cursors[view.buffer];
                cursor.column = lineLength(
                    buffer->second,
                    cursor.line);
                emitCursor(result, viewId, view);
            }
        }
        const Mode before = effectiveMode();
        beginInsertUndoBlock(view.buffer);
        baseMode = Mode::Insert;
        pendingOperator.reset();
        prefix = CommandPrefix::None;
        RepeatChange repeat;
        // Dot repeats the insertion at the current cursor; it must not
        // jump back to the (now updated) '^ mark a second time.
        repeat.command = command == Command::InsertAtLastPosition
            ? Command::EnterInsert
            : command;
        repeat.count = count;
        beginInsertRepeat(
            std::move(repeat),
            offset(
                foundBuffer->second,
                view.cursors[view.buffer]),
            false);
        emitModeIfChanged(result, before);
        break;
    }
    case Command::EnterReplaceMode: {
        if (view.kind != ViewKind::Editor
            || !hasBuffer) {
            return;
        }
        if (foundBuffer->second.readOnly) {
            Event error;
            error.type = EventType::InputError;
            error.view = viewId;
            error.buffer = view.buffer;
            error.message = "buffer is read-only";
            result.events.push_back(std::move(error));
            return;
        }
        const Mode before = effectiveMode();
        beginInsertUndoBlock(view.buffer);
        baseMode = Mode::Replace;
        pendingOperator.reset();
        prefix = CommandPrefix::None;
        replaceSession = ReplaceSession{
            viewId,
            view.buffer,
            std::max<std::size_t>(1, count),
            true,
            {},
            {}};
        emitModeIfChanged(result, before);
        break;
    }
    case Command::EnterVisual:
    case Command::EnterVisualLine:
    case Command::EnterVisualBlock: {
        if (view.kind != ViewKind::Editor
            || !hasBuffer) {
            return;
        }
        const Mode before = effectiveMode();
        const VisualKind requestedKind =
            command == Command::EnterVisualLine
            ? VisualKind::Line
            : command == Command::EnterVisualBlock
            ? VisualKind::Block
            : VisualKind::Character;
        if (baseMode != Mode::Visual) {
            baseMode = Mode::Visual;
            const DisplayPosition display =
                currentDisplayPosition(
                    view, foundBuffer->second);
            visualSelection = VisualSelection{
                viewId,
                view.buffer,
                view.cursors[view.buffer],
                display.column,
                requestedKind};
        } else if (
            visualSelection
            && visualSelection->kind
                == requestedKind) {
            leaveVisualMode();
        } else if (visualSelection) {
            visualSelection->kind =
                requestedKind;
            visualSelection->anchorDisplayColumn =
                displayColumnForBufferColumn(
                    foundBuffer->second,
                    visualSelection->anchor.line,
                    visualSelection->anchor.column);
        }
        pendingOperator.reset();
        emitModeIfChanged(result, before);
        break;
    }
    case Command::PreviousBuffer:
        activateRelativeBuffer(
            result, view, viewId, -1);
        break;
    case Command::NextBuffer:
        activateRelativeBuffer(
            result, view, viewId, 1);
        break;
    case Command::AlternateBuffer:
        if (view.kind == ViewKind::Editor) {
            activateAlternateBuffer(
                result,
                view,
                viewId,
                count,
                countWasExplicit);
        }
        break;
    case Command::AddNumber:
    case Command::SubtractNumber:
        (void)changeNumber(
            result,
            viewId,
            count,
            command == Command::SubtractNumber,
            true);
        break;
    case Command::ShowBufferStatus:
        showBufferStatus(result, viewId);
        break;
    case Command::ShowDetailedBufferStatus:
        showDetailedBufferStatus(result, viewId);
        break;
    case Command::BeginLowercaseOperator:
    case Command::BeginUppercaseOperator:
        normalCount = countWasExplicit ? count : 0;
        beginOperator(
            result,
            viewId,
            command == Command::BeginLowercaseOperator
                ? OperatorKind::Lowercase
                : OperatorKind::Uppercase);
        break;
    case Command::Undo:
        if (view.kind == ViewKind::Editor) {
            undoOrRedo(
                result, viewId, count, false);
        }
        break;
    case Command::Redo:
        if (view.kind == ViewKind::Editor) {
            undoOrRedo(
                result, viewId, count, true);
        }
        break;
    case Command::ScrollHalfDown:
    case Command::ScrollHalfUp:
        scrollHalfPage(
            result,
            view,
            viewId,
            command == Command::ScrollHalfDown,
            count,
            countWasExplicit);
        break;
    case Command::ScrollPageDown:
    case Command::ScrollPageUp:
        scrollFullPage(
            result,
            view,
            viewId,
            command == Command::ScrollPageDown,
            count);
        break;
    case Command::ScrollLineDown:
    case Command::ScrollLineUp:
        scrollSingleLine(
            result,
            view,
            viewId,
            command == Command::ScrollLineDown,
            count);
        break;
    case Command::ViewCursorAtTop:
    case Command::ViewCursorAtCenter:
    case Command::ViewCursorAtBottom:
        positionViewport(
            result, view, viewId, command);
        break;
    case Command::ViewScrollLeft:
    case Command::ViewScrollRight:
    case Command::ViewScrollHalfLeft:
    case Command::ViewScrollHalfRight:
    case Command::ViewCursorAtStart:
    case Command::ViewCursorAtEnd:
        scrollHorizontalViewport(
            result, view, viewId, command, count, countWasExplicit);
        break;
    case Command::BeginViewportPosition:
        pendingArgument = PendingArgument{
            PendingArgumentKind::ViewportPosition,
            viewId,
            view.buffer,
            count,
            countWasExplicit};
        break;
    case Command::BeginRegisterSelect:
        pendingArgument = PendingArgument{
            PendingArgumentKind::SelectRegister,
            viewId,
            view.buffer,
            count,
            countWasExplicit};
        if (countWasExplicit) {
            normalCount = count;
        }
        break;
    case Command::BeginReplace:
        if (view.kind == ViewKind::Editor
            && hasBuffer) {
            pendingArgument = PendingArgument{
                PendingArgumentKind::ReplaceCharacter,
                viewId,
                view.buffer,
                count,
                countWasExplicit};
        }
        break;
    case Command::DeleteUnderCursor:
        (void)deleteCharacters(
            result,
            viewId,
            count,
            false,
            false,
            command);
        break;
    case Command::DeleteBeforeCursor:
        (void)deleteCharacters(
            result,
            viewId,
            count,
            true,
            false,
            command);
        break;
    case Command::PutAfter:
    case Command::PutBefore: {
        const char32_t putRegister = selectedRegister;
        const RegisterValue payload =
            registerValue(putRegister);
        selectedRegister = U'"';
        (void)put(
            result,
            viewId,
            payload,
            count,
            command == Command::PutAfter,
            true,
            putRegister);
        break;
    }
    case Command::OpenBelow:
    case Command::OpenAbove:
        (void)openLine(
            result,
            viewId,
            count,
            command == Command::OpenBelow,
            command);
        break;
    case Command::SubstituteCharacters:
        (void)deleteCharacters(
            result,
            viewId,
            count,
            false,
            true,
            command);
        break;
    case Command::SubstituteLines:
        (void)substituteLines(
            result,
            viewId,
            count,
            command);
        break;
    case Command::DeleteToLineEnd:
    case Command::ChangeToLineEnd:
        (void)changeToLineEnd(
            result,
            viewId,
            command == Command::ChangeToLineEnd,
            count,
            command);
        break;
    case Command::JoinLines:
        (void)joinLines(
            result,
            viewId,
            countWasExplicit
                ? std::max<std::size_t>(2, count)
                : 2,
            true);
        break;
    case Command::ToggleCase:
        (void)toggleCase(
            result,
            viewId,
            count,
            true);
        break;
    case Command::RepeatLastChange:
        repeatLastChange(
            result,
            viewId,
            count,
            countWasExplicit);
        break;
    case Command::BeginSetMark:
    case Command::BeginJumpMarkLine:
    case Command::BeginJumpMarkExact:
        if (view.kind == ViewKind::Editor
            && hasBuffer) {
            pendingArgument = PendingArgument{
                command == Command::BeginSetMark
                    ? PendingArgumentKind::SetMark
                    : command
                            == Command::BeginJumpMarkLine
                    ? PendingArgumentKind::JumpMarkLine
                    : PendingArgumentKind::JumpMarkExact,
                viewId,
                view.buffer,
                count,
                countWasExplicit};
        }
        break;
    case Command::JumpListBack:
    case Command::JumpListForward:
        moveInJumpList(
            result,
            viewId,
            command == Command::JumpListForward,
            count);
        break;
    case Command::ChangeListBack:
    case Command::ChangeListForward:
        moveInChangeList(
            result,
            viewId,
            command == Command::ChangeListForward,
            count);
        break;
    case Command::BeginExCommand:
        requestCommandLine(
            result,
            viewId,
            CommandLineKind::Ex,
            count,
            countWasExplicit);
        break;
    case Command::BeginSearchForward:
    case Command::BeginSearchBackward:
        requestCommandLine(
            result,
            viewId,
            command == Command::BeginSearchForward
                ? CommandLineKind::SearchForward
                : CommandLineKind::SearchBackward,
            count,
            countWasExplicit);
        break;
    case Command::RepeatSearch:
    case Command::RepeatSearchOpposite:
        if (!lastSearchPattern.empty()) {
            searchBuffer(
                result,
                viewId,
                lastSearchPattern,
                command == Command::RepeatSearch
                    ? lastSearchForward
                    : !lastSearchForward,
                count,
                false);
        }
        break;
    case Command::SearchWordForwardExact:
    case Command::SearchWordBackwardExact:
    case Command::SearchWordForwardPartial:
    case Command::SearchWordBackwardPartial:
        searchWordUnderCursor(
            result,
            viewId,
            command == Command::SearchWordForwardExact
                || command
                    == Command::SearchWordForwardPartial,
            command == Command::SearchWordForwardExact
                || command
                    == Command::SearchWordBackwardExact,
            count);
        break;
    case Command::BeginMacroRecord:
        if (!recordingMacro) {
            pendingArgument = PendingArgument{
                PendingArgumentKind::MacroRecord,
                viewId,
                view.buffer,
                count,
                countWasExplicit};
        }
        break;
    case Command::BeginMacroExecute:
        pendingArgument = PendingArgument{
            PendingArgumentKind::MacroExecute,
            viewId,
            view.buffer,
            count,
            countWasExplicit};
        break;
    case Command::RepeatMacroExecute:
        if (lastPlayedMacro) {
            executeMacro(
                result,
                viewId,
                viewId,
                *lastPlayedMacro,
                count);
        }
        break;
    }
}

void VkCore::Implementation::beginOperator(
    DispatchResult &result,
    const ViewId viewId,
    const OperatorKind kind)
{
    const auto found = views.find(viewId);
    if (found == views.end()
        || found->second.kind != ViewKind::Editor
        || found->second.buffer == 0
        || !buffers.contains(found->second.buffer)) {
        return;
    }
    const View &view = found->second;
    const Buffer &buffer = buffers.at(view.buffer);
    if (kind != OperatorKind::Yank
        && buffer.readOnly) {
        Event error;
        error.type = EventType::InputError;
        error.view = viewId;
        error.buffer = view.buffer;
        error.message = "buffer is read-only";
        result.events.push_back(std::move(error));
        normalCount = 0;
        prefix = CommandPrefix::None;
        return;
    }
    const Mode before = effectiveMode();
    pendingOperator = PendingOperator{
        kind,
        view.buffer,
        view.cursors.contains(view.buffer)
            ? view.cursors.at(view.buffer)
            : Cursor{},
        std::max<std::size_t>(1, normalCount),
        normalCount != 0};
    normalCount = 0;
    prefix = CommandPrefix::None;
    emitModeIfChanged(result, before);
}

void VkCore::Implementation::closeInsertUndoBlock()
{
    if (!insertUndoBuffer) {
        return;
    }
    const auto found = buffers.find(*insertUndoBuffer);
    if (found != buffers.end()) {
        found->second.undo.openInsertNode.reset();
    }
    insertUndoBuffer.reset();
}

void VkCore::Implementation::beginInsertUndoBlock(const BufferId bufferId)
{
    if (insertUndoBuffer == bufferId) {
        return;
    }
    closeInsertUndoBlock();
    if (buffers.contains(bufferId)) {
        insertUndoBuffer = bufferId;
    }
}

void VkCore::Implementation::continueInsertUndoBlock(const WindowId windowId)
{
    if (baseMode != Mode::Insert
        && baseMode != Mode::Replace) {
        closeInsertUndoBlock();
        return;
    }
    const auto found = views.find(windowId);
    if (found == views.end()
        || found->second.kind != ViewKind::Editor
        || found->second.buffer == 0
        || !buffers.contains(found->second.buffer)) {
        closeInsertUndoBlock();
        return;
    }
    beginInsertUndoBlock(found->second.buffer);
}

[[nodiscard]] bool VkCore::Implementation::replaceOverwrite(
    DispatchResult &result,
    const WindowId windowId,
    std::u16string inserted,
    const bool recordAction)
{
    const auto foundView = views.find(windowId);
    if (baseMode != Mode::Replace
        || !replaceSession
        || foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer
            != replaceSession->buffer
        || replaceSession->window != windowId) {
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
    const Cursor before = clampCursor(
        buffer, view.cursors[view.buffer], true);
    const std::size_t start = offset(buffer, before);
    std::size_t end = start;
    // Replace-mode Enter splits the line; it never consumes the
    // character under the cursor. Ordinary scalars overwrite exactly one
    // Unicode scalar and append after EOL.
    if (inserted != u"\n"
        && before.column < lineLength(buffer, before.line)) {
        end = buffer.lineStarts[before.line]
            + nextColumnAllowEnd(
                buffer, before.line, before.column);
    }
    const std::u16string removed =
        buffer.text().substr(start, end - start);
    const Cursor after = inserted == u"\n"
        ? Cursor{before.line + 1, 0}
        : Cursor{
              before.line,
              before.column + inserted.size()};
    const bool changesText = removed != inserted;
    if (!mutateBuffer(
            &result,
            view.buffer,
            start,
            end,
            inserted,
            std::pair<WindowId, Cursor>{
                windowId, after})) {
        return false;
    }
    if (!changesText) {
        view.cursors[view.buffer] = after;
        view.displayColumns.erase(view.buffer);
        view.preferredColumn.reset();
        emitCursor(result, windowId, view);
    }

    std::size_t actionIndex = replaceSession->actions.size();
    if (recordAction) {
        replaceSession->actions.push_back(
            ReplaceAction{
                ReplaceActionKind::Overwrite,
                inserted});
    }
    replaceSession->steps.push_back(
        ReplaceStep{
            start,
            removed,
            std::move(inserted),
            before,
            after,
            actionIndex});
    return true;
}

[[nodiscard]] bool VkCore::Implementation::replaceDelete(
    DispatchResult &result,
    const WindowId windowId,
    const bool recordAction)
{
    const auto foundView = views.find(windowId);
    if (baseMode != Mode::Replace
        || !replaceSession
        || foundView == views.end()
        || foundView->second.buffer
            != replaceSession->buffer
        || replaceSession->window != windowId) {
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
    const Cursor cursor = clampCursor(
        buffer, view.cursors[view.buffer], true);
    if (cursor.column >= lineLength(
            buffer, cursor.line)) {
        return false;
    }
    const std::size_t start = offset(buffer, cursor);
    const std::size_t end = buffer.lineStarts[cursor.line]
        + nextColumnAllowEnd(
            buffer, cursor.line, cursor.column);
    if (!mutateBuffer(
            &result,
            view.buffer,
            start,
            end,
            {},
            std::pair<WindowId, Cursor>{
                windowId, cursor})) {
        return false;
    }
    if (recordAction) {
        replaceSession->actions.push_back(
            ReplaceAction{ReplaceActionKind::Delete, {}});
    }
    return true;
}

[[nodiscard]] bool VkCore::Implementation::replaceBackspace(
    DispatchResult &result,
    const WindowId windowId)
{
    if (baseMode != Mode::Replace
        || !replaceSession
        || replaceSession->window != windowId
        || replaceSession->steps.empty()) {
        return false;
    }
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.buffer
            != replaceSession->buffer) {
        return false;
    }
    View &view = foundView->second;
    auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return false;
    }
    ReplaceStep step = std::move(
        replaceSession->steps.back());
    const Cursor current = clampCursor(
        foundBuffer->second,
        view.cursors[view.buffer],
        true);
    if (current != step.after) {
        return false;
    }
    replaceSession->steps.pop_back();
    const bool changesText = step.inserted != step.removed;
    if (!mutateBuffer(
            &result,
            view.buffer,
            step.offset,
            step.offset + step.inserted.size(),
            step.removed,
            std::pair<WindowId, Cursor>{
                windowId, step.before})) {
        return false;
    }
    if (!changesText) {
        view.cursors[view.buffer] = step.before;
        view.displayColumns.erase(view.buffer);
        view.preferredColumn.reset();
        emitCursor(result, windowId, view);
    }
    if (step.actionIndex < replaceSession->actions.size()
        && replaceSession->actions[step.actionIndex].kind
            == ReplaceActionKind::Overwrite) {
        replaceSession->actions.erase(
            replaceSession->actions.begin()
            + static_cast<std::ptrdiff_t>(
                step.actionIndex));
        for (ReplaceStep &remaining : replaceSession->steps) {
            if (remaining.actionIndex > step.actionIndex) {
                --remaining.actionIndex;
            }
        }
    }
    return true;
}

void VkCore::Implementation::rememberReplaceSegment()
{
    if (!replaceSession
        || replaceSession->actions.empty()) {
        return;
    }
    RepeatChange repeat;
    repeat.command = Command::EnterReplaceMode;
    repeat.count = replaceSession->repeatCountActive
        ? std::max<std::size_t>(
              1, replaceSession->repeatCount)
        : 1;
    repeat.replaceActions = replaceSession->actions;
    rememberChange(std::move(repeat));
}

void VkCore::Implementation::beginNewReplaceSegment(
    const WindowId windowId,
    const bool preservePreviousForDot)
{
    if (!replaceSession
        || replaceSession->window != windowId) {
        return;
    }
    if (preservePreviousForDot) {
        rememberReplaceSegment();
    }
    closeInsertUndoBlock();
    replaceSession->steps.clear();
    replaceSession->actions.clear();
    replaceSession->repeatCount = 1;
    replaceSession->repeatCountActive = false;
    beginInsertUndoBlock(replaceSession->buffer);
}

void VkCore::Implementation::replayReplaceActions(
    DispatchResult &result,
    const WindowId windowId,
    const std::vector<ReplaceAction> &actions,
    const std::size_t count)
{
    for (std::size_t repetition = 0;
         repetition < count;
         ++repetition) {
        for (const ReplaceAction &action : actions) {
            if (action.kind
                == ReplaceActionKind::Delete) {
                (void)replaceDelete(
                    result, windowId, false);
            } else {
                (void)replaceOverwrite(
                    result,
                    windowId,
                    action.text,
                    false);
            }
        }
    }
}

void VkCore::Implementation::finishReplaceMode(
    DispatchResult *const result,
    const WindowId windowId,
    const bool applyCount)
{
    if (baseMode != Mode::Replace) {
        replaceSession.reset();
        return;
    }
    const Mode beforeMode = effectiveMode();
    if (replaceSession
        && replaceSession->window == windowId) {
        const std::vector<ReplaceAction> actions =
            replaceSession->actions;
        const std::size_t repeats =
            applyCount
                && replaceSession->repeatCountActive
            ? std::max<std::size_t>(
                  1, replaceSession->repeatCount)
            : 1;
        if (result != nullptr
            && repeats > 1
            && !actions.empty()) {
            replayReplaceActions(
                *result,
                windowId,
                actions,
                repeats - 1);
        }
        rememberReplaceSegment();
    }
    closeInsertUndoBlock();
    baseMode = Mode::Normal;
    replaceSession.reset();
    const auto foundView = views.find(windowId);
    if (foundView != views.end()
        && foundView->second.buffer != 0) {
        View &view = foundView->second;
        const auto foundBuffer = buffers.find(view.buffer);
        if (foundBuffer != buffers.end()) {
            Cursor &cursor = view.cursors[view.buffer];
            cursor = clampCursor(
                foundBuffer->second, cursor, true);
            const std::size_t insertExitOffset = offset(
                foundBuffer->second, cursor);
            // Vim's Replace cursor is an insertion boundary. Leaving R
            // lands on the preceding character, even for an empty R
            // session; column zero is already a valid Normal position.
            if (cursor.column > 0) {
                cursor.column = previousColumnAllowEnd(
                    foundBuffer->second,
                    cursor.line,
                    cursor.column);
            }
            cursor = clampCursor(
                foundBuffer->second, cursor, false);
            foundBuffer->second.lastInsertExitMark =
                insertExitOffset;
            view.displayColumns.erase(view.buffer);
            view.preferredColumn.reset();
            if (result != nullptr) {
                emitCursor(*result, windowId, view);
            }
        }
    }
    if (result != nullptr) {
        emitModeIfChanged(*result, beforeMode);
    }
}

void VkCore::Implementation::beginInsertOneNormal(
    DispatchResult &result,
    const WindowId windowId)
{
    if (baseMode != Mode::Insert
        || insertOneNormalPending) {
        return;
    }
    const Mode before = effectiveMode();
    keywordCompletion.reset();
    if (activeInsertRepeat) {
        // The outer Insert count does not repeat across CTRL-O. The text
        // before it is nevertheless a complete dot-repeat transaction.
        activeInsertRepeat->count = 1;
        finishInsertRepeat(&result);
    }
    closeInsertUndoBlock();
    baseMode = Mode::Normal;
    insertOneNormalPending = true;
    insertOneNormalSawKey = false;
    normalCount = 0;
    prefix = CommandPrefix::None;
    pendingOperator.reset();
    pendingArgument.reset();
    emitModeIfChanged(result, before);
    pendingView = windowId;
}

void VkCore::Implementation::finishInsertOneNormal(
    DispatchResult &result,
    const WindowId windowId)
{
    if (!insertOneNormalPending) {
        return;
    }
    const Mode before = effectiveMode();
    if (baseMode == Mode::Visual) {
        leaveVisualMode();
    }
    // An Insert/Replace command executed through CTRL-O takes ownership
    // as the new edit session. Otherwise resume the originating Insert.
    if (baseMode != Mode::Insert
        && baseMode != Mode::Replace) {
        baseMode = Mode::Insert;
    }
    insertOneNormalPending = false;
    insertOneNormalSawKey = false;
    pendingOperator.reset();
    pendingArgument.reset();
    normalCount = 0;
    prefix = CommandPrefix::None;
    continueInsertUndoBlock(windowId);
    const auto foundView = views.find(windowId);
    if (baseMode == Mode::Insert
        && !activeInsertRepeat
        && foundView != views.end()
        && foundView->second.kind == ViewKind::Editor
        && foundView->second.buffer != 0) {
        const auto foundBuffer = buffers.find(
            foundView->second.buffer);
        if (foundBuffer != buffers.end()) {
            RepeatChange repeat;
            repeat.command = Command::EnterInsert;
            beginInsertRepeat(
                std::move(repeat),
                offset(
                    foundBuffer->second,
                    foundView->second.cursors[
                        foundView->second.buffer]),
                false);
        }
    }
    emitModeIfChanged(result, before);
}

void VkCore::Implementation::moveReplaceCursor(
    DispatchResult &result,
    const WindowId windowId,
    const SpecialKey key)
{
    const auto foundView = views.find(windowId);
    if (baseMode != Mode::Replace
        || !replaceSession
        || foundView == views.end()
        || foundView->second.buffer
            != replaceSession->buffer) {
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    Cursor cursor = clampCursor(
        buffer, view.cursors[view.buffer], true);
    if (key == SpecialKey::Left) {
        cursor.column = previousColumnAllowEnd(
            buffer, cursor.line, cursor.column);
        view.preferredColumn.reset();
    } else if (key == SpecialKey::Right) {
        cursor.column = cursor.column
                < lineLength(buffer, cursor.line)
            ? nextColumnAllowEnd(
                  buffer, cursor.line, cursor.column)
            : cursor.column;
        view.preferredColumn.reset();
    } else if (key == SpecialKey::Home) {
        cursor.column = 0;
        view.preferredColumn.reset();
    } else if (key == SpecialKey::End) {
        cursor.column = lineLength(buffer, cursor.line);
        view.preferredColumn.reset();
    } else if (key == SpecialKey::Up
               || key == SpecialKey::Down
               || key == SpecialKey::PageUp
               || key == SpecialKey::PageDown) {
        const std::size_t rows =
            key == SpecialKey::PageUp
                || key == SpecialKey::PageDown
            ? std::max<std::size_t>(1, view.viewportRows)
            : 1;
        const std::size_t goal =
            view.preferredColumn.value_or(cursor.column);
        if (key == SpecialKey::Up
            || key == SpecialKey::PageUp) {
            cursor.line = cursor.line > rows
                ? cursor.line - rows
                : 0;
        } else {
            cursor.line = std::min(
                buffer.lineStarts.size() - 1,
                cursor.line + rows);
        }
        cursor.column = safeColumn(
            buffer, cursor.line, goal, true);
        view.preferredColumn = goal;
    }
    view.cursors[view.buffer] = cursor;
    view.displayColumns.erase(view.buffer);
    emitCursor(result, windowId, view);
    // Vim splits both undo and dot-repeat at cursor keys while retaining
    // Replace mode. A counted R also stops repeating at this boundary.
    beginNewReplaceSegment(windowId, true);
}

} // namespace vkui::vk
