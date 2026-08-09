#include "VkCoreInternal.h"

namespace vkui::vk {

[[nodiscard]] constexpr bool VkCore::Implementation::transformOperator(
    const OperatorKind kind) noexcept
{
    return kind == OperatorKind::ShiftRight
        || kind == OperatorKind::ShiftLeft
        || kind == OperatorKind::Lowercase
        || kind == OperatorKind::Uppercase;
}

[[nodiscard]] std::u16string VkCore::Implementation::shiftedLines(
    const Buffer &buffer,
    const std::size_t firstLine,
    const std::size_t lastLine,
    const bool right,
    const std::size_t levels) const
{
    std::u16string replacement;
    const std::size_t shift = std::max<std::size_t>(
        1, buffer.tabStop);
    for (std::size_t line = firstLine;
         line <= lastLine;
         ++line) {
        const std::size_t start = buffer.lineStarts[line];
        const std::size_t length = lineLength(buffer, line);
        std::u16string_view content{
            buffer.text().data() + start, length};
        std::size_t contentStart = 0;
        std::size_t indentation = 0;
        while (contentStart < content.size()) {
            if (content[contentStart] == u' ') {
                ++indentation;
                ++contentStart;
            } else if (content[contentStart] == u'\t') {
                indentation += shift
                    - indentation % shift;
                ++contentStart;
            } else {
                break;
            }
        }
        if (contentStart != content.size()) {
            const std::size_t amount = levels
                    > std::numeric_limits<std::size_t>::max()
                        / shift
                ? std::numeric_limits<std::size_t>::max()
                : levels * shift;
            const std::size_t targetIndentation = right
                ? indentation
                    + std::min(
                        amount,
                        std::numeric_limits<std::size_t>::max()
                            - indentation)
                : indentation > amount
                ? indentation - amount
                : 0;
            replacement.append(targetIndentation, u' ');
            replacement.append(
                content.substr(contentStart));
        }
        if (line != lastLine) {
            replacement.push_back(u'\n');
        }
    }
    return replacement;
}

[[nodiscard]] std::u16string VkCore::Implementation::transformedOperatorText(
    const Buffer &buffer,
    const OperatorKind operation,
    const std::size_t start,
    const std::size_t end,
    std::size_t *const normalizedStart,
    std::size_t *const normalizedEnd,
    std::size_t *const firstLine) const
{
    if (operation == OperatorKind::Lowercase
        || operation == OperatorKind::Uppercase) {
        const QString original = QString::fromUtf16(
            buffer.text().data() + start,
            static_cast<qsizetype>(end - start));
        const QString transformed =
            operation == OperatorKind::Lowercase
            ? original.toLower()
            : original.toUpper();
        *normalizedStart = start;
        *normalizedEnd = end;
        *firstLine = cursorAtOffset(
            buffer, start, true).line;
        return transformed.toStdU16String();
    }

    const std::size_t first = cursorAtOffset(
        buffer, start, true).line;
    const std::size_t finalOffset = end > start
        ? previousScalarOffset(
              buffer.text(), end)
        : start;
    const std::size_t last = cursorAtOffset(
        buffer, finalOffset, true).line;
    *normalizedStart = buffer.lineStarts[first];
    *normalizedEnd = buffer.lineStarts[last]
        + lineLength(buffer, last);
    *firstLine = first;
    return shiftedLines(
        buffer,
        first,
        last,
        operation == OperatorKind::ShiftRight);
}

void VkCore::Implementation::finishOperator(
    DispatchResult &result,
    const ViewId viewId,
    std::size_t rangeStart,
    std::size_t rangeEnd,
    const bool linewise,
    std::u16string registerText,
    std::optional<RepeatChange> repeat)
{
    if (!pendingOperator) {
        return;
    }
    const PendingOperator operation =
        *pendingOperator;
    const Mode before = effectiveMode();
    pendingOperator.reset();
    normalCount = 0;
    prefix = CommandPrefix::None;

    const auto foundBuffer =
        buffers.find(operation.buffer);
    if (foundBuffer == buffers.end()) {
        emitModeIfChanged(result, before);
        return;
    }
    Buffer &buffer = foundBuffer->second;
    rangeStart = std::min(
        rangeStart, buffer.text().size());
    rangeEnd = std::clamp(
        rangeEnd,
        rangeStart,
        buffer.text().size());
    if (transformOperator(operation.kind)) {
        std::size_t normalizedStart = rangeStart;
        std::size_t normalizedEnd = rangeEnd;
        std::size_t firstLine = 0;
        std::u16string replacement =
            transformedOperatorText(
                buffer,
                operation.kind,
                rangeStart,
                rangeEnd,
                &normalizedStart,
                &normalizedEnd,
                &firstLine);
        const bool changed =
            normalizedEnd - normalizedStart
                != replacement.size()
            || !std::equal(
                replacement.cbegin(),
                replacement.cend(),
                buffer.text().cbegin()
                    + static_cast<std::ptrdiff_t>(
                        normalizedStart));
        bool applied = true;
        if (changed) {
            std::size_t shiftedColumn = 0;
            while (shiftedColumn < replacement.size()
                   && replacement[shiftedColumn] != u'\n'
                   && (replacement[shiftedColumn] == u' '
                       || replacement[shiftedColumn]
                           == u'\t')) {
                ++shiftedColumn;
            }
            const Cursor target{
                firstLine,
                operation.kind == OperatorKind::ShiftRight
                        || operation.kind
                            == OperatorKind::ShiftLeft
                    ? shiftedColumn
                    : cursorAtOffset(
                          buffer, normalizedStart).column};
            applied = mutateBuffer(
                    &result,
                    operation.buffer,
                    normalizedStart,
                    normalizedEnd,
                    std::move(replacement),
                    std::pair<WindowId, Cursor>{
                        viewId, target});
        }
        // Vim records a completed transform operator even when the
        // selected text is already in the requested case (or an empty
        // line cannot shift). Dot must still apply that recipe at the
        // next cursor position without manufacturing an undo node here.
        if (applied && repeat) {
            repeat->registerName = selectedRegister;
            rememberChange(std::move(*repeat));
        }
        selectedRegister = U'"';
        emitModeIfChanged(result, before);
        const auto transformedView = views.find(viewId);
        if (transformedView != views.end()) {
            emitCursor(
                result, viewId, transformedView->second);
        }
        return;
    }
    if (registerText.empty()
        && rangeStart != rangeEnd) {
        registerText =
            buffer.text().substr(
                rangeStart,
                rangeEnd - rangeStart);
    }
    if (operation.kind == OperatorKind::Yank
        && rangeStart != rangeEnd) {
        buffer.lastOperationStartMark = rangeStart;
        buffer.lastOperationEndMark = rangeEnd - 1;
        buffer.lastOperationUndoNode.reset();
    }
    const char32_t operationRegister = selectedRegister;
    writeOperationRegister(
        operation.kind,
        std::move(registerText),
        linewise);
    if (repeat) {
        repeat->registerName = operationRegister;
    }

    bool editApplied = true;
    if (operation.kind == OperatorKind::Change) {
        beginInsertUndoBlock(operation.buffer);
        baseMode = Mode::Insert;
    }
    if (operation.kind != OperatorKind::Yank
        && rangeStart != rangeEnd) {
        editApplied = applyBufferEdit(
            result,
            operation.buffer,
            rangeStart,
            rangeEnd,
            {});
    }
    if (operation.kind == OperatorKind::Change
        && editApplied) {
        const auto changedView = views.find(viewId);
        if (changedView != views.end()
            && changedView->second.buffer
                == operation.buffer) {
            RepeatChange insertRepeat =
                repeat.value_or(RepeatChange{});
            insertRepeat.operation =
                OperatorKind::Change;
            beginInsertRepeat(
                std::move(insertRepeat),
                offset(
                    buffers.at(operation.buffer),
                    changedView->second
                        .cursors[operation.buffer]),
                rangeStart != rangeEnd);
        }
    } else if (operation.kind
               == OperatorKind::Change) {
        baseMode = Mode::Normal;
        closeInsertUndoBlock();
    } else if (operation.kind
                   == OperatorKind::Delete
               && editApplied
               && rangeStart != rangeEnd
               && repeat) {
        rememberChange(std::move(*repeat));
    }
    emitModeIfChanged(result, before);
    const auto foundView = views.find(viewId);
    if (foundView != views.end()) {
        emitCursor(result, viewId, foundView->second);
    }
}

void VkCore::Implementation::finishLinewiseOperator(
    DispatchResult &result,
    const ViewId viewId)
{
    if (!pendingOperator) {
        return;
    }
    const auto foundBuffer =
        buffers.find(pendingOperator->buffer);
    if (foundBuffer == buffers.end()) {
        finishOperator(result, viewId, 0, 0, true);
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const std::size_t firstLine = std::min(
        pendingOperator->start.line,
        buffer.lineStarts.size() - 1);
    const std::size_t lineCount =
        pendingOperator->count
        * std::max<std::size_t>(1, normalCount);
    const std::size_t afterLine = std::min(
        buffer.lineStarts.size(),
        firstLine + lineCount);
    std::size_t start = buffer.lineStarts[firstLine];
    const std::size_t end =
        afterLine < buffer.lineStarts.size()
        ? buffer.lineStarts[afterLine]
        : buffer.text().size();
    if (end == buffer.text().size()
        && start > 0
        && (pendingOperator->kind
                == OperatorKind::Delete
            || pendingOperator->kind
                == OperatorKind::Change)) {
        --start;
    }
    RepeatChange repeat;
    repeat.form = RepeatForm::OperatorLinewise;
    repeat.operation = pendingOperator->kind;
    repeat.count = lineCount;
    finishOperator(
        result,
        viewId,
        start,
        end,
        true,
        linewiseText(
            buffer,
            firstLine,
            std::max(
                firstLine,
                afterLine - 1)),
        std::move(repeat));
}

void VkCore::Implementation::cancelPendingOperatorMotion(
    DispatchResult &result)
{
    const Mode before = effectiveMode();
    pendingOperator.reset();
    normalCount = 0;
    prefix = CommandPrefix::None;
    emitModeIfChanged(result, before);
}

void VkCore::Implementation::finishCharacterwiseOperator(
    DispatchResult &result,
    const WindowId windowId,
    const Cursor origin,
    const Cursor target,
    const bool inclusive,
    std::optional<RepeatChange> repeat,
    const std::optional<std::size_t>
        forwardEndOverride)
{
    if (!pendingOperator) {
        return;
    }
    const auto foundBuffer =
        buffers.find(pendingOperator->buffer);
    if (foundBuffer == buffers.end()) {
        finishOperator(
            result, windowId, 0, 0, false);
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const std::size_t originOffset =
        offset(buffer, origin);
    const std::size_t targetOffset =
        offset(buffer, target);
    std::size_t start = 0;
    std::size_t end = 0;
    if (forwardEndOverride) {
        start = std::min(
            originOffset, *forwardEndOverride);
        end = std::max(
            originOffset, *forwardEndOverride);
    } else if (targetOffset >= originOffset) {
        start = originOffset;
        end = inclusive
            ? cursorCharacterEnd(buffer, target)
            : targetOffset;
    } else {
        start = targetOffset;
        end = inclusive
            ? cursorCharacterEnd(buffer, origin)
            : originOffset;
    }
    finishOperator(
        result,
        windowId,
        start,
        end,
        false,
        {},
        std::move(repeat));
}

void VkCore::Implementation::applyResolvedDisplayMotion(
    DispatchResult &result,
    const PendingDisplayMotion &transaction,
    const DisplayMotionResolution &resolution)
{
    const auto foundView = views.find(transaction.window);
    if (foundView == views.end()) {
        return;
    }
    View &view = foundView->second;
    if (pendingOperator) {
        RepeatChange repeat;
        repeat.form = RepeatForm::OperatorMotion;
        repeat.operation = pendingOperator->kind;
        repeat.motion = transaction.command;
        repeat.count = transaction.count;
        repeat.motionCountWasExplicit =
            transaction.countWasExplicit;
        finishCharacterwiseOperator(
            result,
            transaction.window,
            transaction.origin,
            resolution.destination.buffer,
            transaction.command
                == Command::DisplayRowEnd,
            std::move(repeat));
        if (!transaction.repeatedInsert.empty()
            && baseMode == Mode::Insert) {
            completeRepeatedInsert(
                result,
                transaction.window,
                transaction.repeatedInsert);
        }
        return;
    }

    setDisplayPosition(view, resolution.destination);
    view.preferredColumn.reset();
    view.preferredDisplayRowColumn =
        verticalDisplayMotion(transaction.command)
        ? resolution.rowGoalColumn
        : std::nullopt;
    emitCursor(result, transaction.window, view);
}

void VkCore::Implementation::finishCharacterSearchMotion(
    DispatchResult &result,
    const WindowId windowId,
    CharacterSearch search,
    const std::size_t count,
    const bool repeated)
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
    const auto foundBuffer =
        buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        abortMacroCommand();
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }
    const Cursor origin = clampCursor(
        foundBuffer->second,
        view.cursors[view.buffer],
        false);
    const std::optional<Cursor> target =
        characterSearchTarget(
            foundBuffer->second,
            origin,
            search,
            count,
            repeated);
    if (!target) {
        abortMacroCommand();
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }

    if (pendingOperator) {
        // searchc() marks forward character motions inclusive and
        // backward motions exclusive. finishOperator remains the sole
        // owner of range mutation and register/undo bookkeeping.
        finishCharacterwiseOperator(
            result,
            windowId,
            pendingOperator->start,
            *target,
            search.forward);
        return;
    }
    view.cursors[view.buffer] = *target;
    view.preferredColumn.reset();
    emitCursor(result, windowId, view);
}

void VkCore::Implementation::applyRepeatedCharacterSearch(
    DispatchResult &result,
    const WindowId windowId,
    const std::size_t count,
    const bool opposite)
{
    if (!lastCharacterSearch) {
        abortMacroCommand();
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }
    CharacterSearch repeated =
        *lastCharacterSearch;
    if (opposite) {
        repeated.forward = !repeated.forward;
    }
    finishCharacterSearchMotion(
        result,
        windowId,
        repeated,
        count,
        true);
}

void VkCore::Implementation::applyMatchingPairMotion(
    DispatchResult &result,
    const WindowId windowId)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer =
        buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }
    const Cursor origin = clampCursor(
        foundBuffer->second,
        view.cursors[view.buffer],
        false);
    const std::optional<Cursor> target =
        matchingPairTarget(
            foundBuffer->second, origin);
    if (!target) {
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return;
    }
    if (pendingOperator) {
        finishCharacterwiseOperator(
            result,
            windowId,
            pendingOperator->start,
            *target,
            true);
        return;
    }
    if (origin != *target) {
        recordJump(
            view,
            Location{
                view.buffer,
                offset(foundBuffer->second, origin)},
            Location{
                view.buffer,
                offset(foundBuffer->second, *target)});
    }
    view.cursors[view.buffer] = *target;
    view.preferredColumn.reset();
    emitCursor(result, windowId, view);
}

void VkCore::Implementation::finishMotionOperator(
    DispatchResult &result,
    const ViewId viewId,
    const Command motion)
{
    if (!pendingOperator) {
        return;
    }
    const auto foundView = views.find(viewId);
    const auto foundBuffer =
        buffers.find(pendingOperator->buffer);
    if (foundView == views.end()
        || foundBuffer == buffers.end()) {
        finishOperator(result, viewId, 0, 0, false);
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const Cursor origin = pendingOperator->start;
    View motionView = foundView->second;
    motionView.cursors[pendingOperator->buffer] = origin;
    const std::size_t motionCount =
        pendingOperator->count
        * std::max<std::size_t>(1, normalCount);
    const bool motionCountWasExplicit =
        pendingOperator->countWasExplicit
        || normalCount != 0;
    if (motion == Command::FilePercent
        && (!motionCountWasExplicit
            || motionCount > 100)) {
        cancelPendingOperatorMotion(result);
        return;
    }
    const bool changeWordForward =
        pendingOperator->kind == OperatorKind::Change
        && (motion == Command::WordForward
            || motion == Command::BigWordForward)
        && wordClass(
               buffer,
               origin.line,
               origin.column,
               motion == Command::BigWordForward)
            != WordClass::White;
    const Command effectiveMotion = changeWordForward
        ? motion == Command::BigWordForward
            ? Command::BigWordEnd
            : Command::WordEnd
        : motion;
    const Cursor target =
        motion == Command::SectionForwardStart
        ? movedSectionCursor(
              buffer,
              origin,
              motionCount,
              true,
              false,
              true)
        : movedCursor(
              motionView,
              effectiveMotion,
              motionCount,
              motionCountWasExplicit);

    const bool linewise = linewiseMotion(motion)
        || pendingOperator->kind
            == OperatorKind::ShiftRight
        || pendingOperator->kind
            == OperatorKind::ShiftLeft;
    if (linewise) {
        const std::size_t firstLine =
            std::min(origin.line, target.line);
        const std::size_t lastLine =
            std::max(origin.line, target.line);
        std::size_t start =
            buffer.lineStarts[firstLine];
        const std::size_t afterLine =
            std::min(
                buffer.lineStarts.size(),
                lastLine + 1);
        const std::size_t end =
            afterLine < buffer.lineStarts.size()
            ? buffer.lineStarts[afterLine]
            : buffer.text().size();
        if (end == buffer.text().size()
            && start > 0
            && (pendingOperator->kind
                    == OperatorKind::Delete
                || pendingOperator->kind
                    == OperatorKind::Change)) {
            --start;
        }
        RepeatChange repeat;
        repeat.form =
            RepeatForm::OperatorMotion;
        repeat.operation =
            pendingOperator->kind;
        repeat.motion = motion;
        repeat.count = motionCount;
        repeat.motionCountWasExplicit =
            motionCountWasExplicit;
        finishOperator(
            result,
            viewId,
            start,
            end,
            true,
            linewiseText(
                buffer,
                firstLine,
                lastLine),
            std::move(repeat));
        return;
    }

    RepeatChange repeat;
    repeat.form = RepeatForm::OperatorMotion;
    repeat.operation = pendingOperator->kind;
    repeat.motion = motion;
    repeat.count = motionCount;
    repeat.motionCountWasExplicit =
        motionCountWasExplicit;
    const std::optional<std::size_t> adjustedEnd =
        (motion == Command::WordForward
         || motion == Command::BigWordForward)
        ? wordForwardOperatorEnd(
              buffer,
              origin,
              motionCount,
              motion == Command::BigWordForward)
        : std::nullopt;
    finishCharacterwiseOperator(
        result,
        viewId,
        origin,
        target,
        inclusiveMotion(effectiveMotion),
        std::move(repeat),
        adjustedEnd);
}

} // namespace vkui::vk
