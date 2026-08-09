#include "VkCoreInternal.h"
#include "VkCoreNativeGrammar.h"

namespace vkui::vk::core_detail {

bool isInsertEditingCommandKey(const KeyAtom &key) noexcept
{
    return findNativeDescriptor(kInsertCommandGrammar, key)
        != nullptr;
}

} // namespace vkui::vk::core_detail

namespace vkui::vk {

[[nodiscard]] bool VkCore::Implementation::consumePendingArgument(
    DispatchResult &result,
    const WindowId windowId,
    const KeyAtom &key)
{
    if (!pendingArgument) {
        return false;
    }
    const PendingArgument argument =
        *pendingArgument;
    pendingArgument.reset();
    if (argument.window != windowId
        || !views.contains(windowId)
        || views.at(windowId).buffer
            != argument.buffer) {
        normalCount = 0;
        selectedRegister = U'"';
        if (pendingOperator) {
            cancelPendingOperatorMotion(result);
        }
        return true;
    }

    if (argument.kind == PendingArgumentKind::SetMark
        || argument.kind
            == PendingArgumentKind::JumpMarkLine
        || argument.kind
            == PendingArgumentKind::JumpMarkExact) {
        const auto name = detail::character(key);
        const bool valid = name
            && (argument.kind
                        == PendingArgumentKind::SetMark
                    ? findNativeDescriptor(
                          kMarkGrammar, key)
                        != nullptr
                    : findNativeDescriptor(
                          kJumpMarkGrammar, key)
                        != nullptr);
        if (!valid) {
            Event error;
            error.type = EventType::InputError;
            error.view = windowId;
            error.buffer = argument.buffer;
            error.message = "invalid mark name";
            result.events.push_back(
                std::move(error));
            return true;
        }
        if (argument.kind
            == PendingArgumentKind::SetMark) {
            setMark(result, windowId, *name);
        } else {
            jumpToMark(
                result,
                windowId,
                *name,
                argument.kind
                    == PendingArgumentKind::JumpMarkLine);
        }
        return true;
    }

    if (argument.kind
            == PendingArgumentKind::MacroRecord
        || argument.kind
            == PendingArgumentKind::MacroExecute) {
        const auto name = detail::character(key);
        if (argument.kind
            == PendingArgumentKind::MacroRecord) {
            if (!name || !startMacroRecording(*name)) {
                Event error;
                error.type = EventType::InputError;
                error.view = windowId;
                error.message =
                    "macro register must be a letter";
                result.events.push_back(
                    std::move(error));
            }
            return true;
        }
        std::optional<std::size_t> macro;
        if (name && *name == U'@') {
            macro = lastPlayedMacro;
        } else if (
            name
            && ((*name >= U'a' && *name <= U'z')
                || (*name >= U'A'
                    && *name <= U'Z'))) {
            macro = static_cast<std::size_t>(
                *name >= U'A' && *name <= U'Z'
                    ? *name - U'A'
                    : *name - U'a');
        }
        if (macro) {
            executeMacro(
                result,
                windowId,
                windowId,
                *macro,
                argument.count);
        }
        return true;
    }

    if (argument.kind
        == PendingArgumentKind::TextObject) {
        const auto object = detail::character(key);
        if (object
            && findNativeDescriptor(
                   kTextObjectGrammar, key)
                != nullptr) {
            applyTextObject(
                result,
                windowId,
                *object,
                argument.around,
                argument.count);
        } else if (pendingOperator) {
            abortMacroCommand();
            cancelPendingOperatorMotion(result);
        }
        return true;
    }

    if (argument.kind
        == PendingArgumentKind::CharacterSearch) {
        const std::optional<char32_t> target =
            nativeKeyMatches(
                kCharacterArgumentHint.key, key)
            ? detail::character(key)
            : std::nullopt;
        if (!target) {
            abortMacroCommand();
            if (pendingOperator) {
                cancelPendingOperatorMotion(result);
            }
            return true;
        }
        CharacterSearch search{
            *target,
            argument.searchForward,
            argument.searchTill};
        // searchc() records a newly supplied target before attempting the
        // scan, so a failed f/F/t/T still becomes the source for ';' and
        // ',' exactly as it does in Neovim.
        lastCharacterSearch = search;
        finishCharacterSearchMotion(
            result,
            windowId,
            search,
            argument.count,
            false);
        return true;
    }

    if (argument.kind
        == PendingArgumentKind::SelectRegister) {
        const auto name = detail::character(key);
        if (findNativeDescriptor(
                kRegisterGrammar, key)
            != nullptr
            && name) {
            selectedRegister = *name;
        } else {
            selectedRegister = U'"';
            normalCount = 0;
        }
        if (insertOneNormalPending) {
            // `"a` only chooses a register; the following Normal command
            // is the single command owned by Insert CTRL-O.
            insertOneNormalSawKey = false;
        }
        return true;
    }

    if (argument.kind
        == PendingArgumentKind::InsertRegister) {
        const auto name = detail::character(key);
        if (name
            && findNativeDescriptor(
                   kRegisterGrammar, key)
                != nullptr) {
            (void)insertRegisterValue(
                result, windowId, *name);
        }
        return true;
    }

    if (argument.kind
        == PendingArgumentKind::ViewportPosition) {
        if (const auto *const descriptor =
                findNativeDescriptor(
                    kViewportGrammar, key)) {
            // Beginning the pending z grammar consumes Normal's count. Put
            // the count back for the complete command before executing its
            // final key (for example, 3zl).
            normalCount = argument.countWasExplicit
                ? argument.count
                : 0;
            execute(
                result,
                windowId,
                descriptor->command);
        }
        return true;
    }

    std::optional<char32_t> replacement;
    if (const auto *const descriptor =
            findNativeDescriptor(
                kReplacementGrammar, key)) {
        replacement =
            descriptor->hint.key.kind
                    == NativeKeyKind::AnyCharacter
            ? detail::character(key)
            : std::optional<char32_t>(
                  descriptor->replacement);
    }
    if (replacement) {
        const auto view = views.find(windowId);
        if (view != views.end()
            && ownsVisualBlock(
                windowId, view->second.buffer)) {
            (void)replaceVisualBlock(
                result,
                windowId,
                *replacement,
                true);
        } else {
            (void)replaceCharacters(
                result,
                windowId,
                argument.count,
                *replacement,
                true);
        }
    }
    return true;
}

void VkCore::Implementation::consumeNormalKey(
    DispatchResult &result,
    const ViewId viewId,
    const InputTargetId inputTarget,
    const KeyAtom &key)
{
    // A Visual selection belongs to one concrete window/buffer pair.
    // Never reinterpret its anchor after focus or buffer activation.
    if (cancelStaleVisual(result, viewId)) {
        return;
    }
    const bool controlCancel =
        modifiedCharacter(
            key, U'c', KeyModifier::Control)
        || modifiedCharacter(
            key, U'[', KeyModifier::Control);
    if (detail::isSpecialKey(key, SpecialKey::Escape)
        || controlCancel) {
        if (insertOneNormalPending) {
            const Mode before = effectiveMode();
            input.clear();
            keywordCompletion.reset();
            pendingOperator.reset();
            pendingArgument.reset();
            normalCount = 0;
            prefix = CommandPrefix::None;
            selectedRegister = U'"';
            if (baseMode == Mode::Visual) {
                leaveVisualMode();
            }
            if (controlCancel) {
                if (baseMode == Mode::Insert) {
                    finishInsertRepeat(&result);
                } else if (baseMode == Mode::Replace) {
                    finishReplaceMode(
                        &result, viewId, false);
                }
                closeInsertUndoBlock();
                replaceSession.reset();
                insertOneNormalPending = false;
                insertOneNormalSawKey = false;
                baseMode = Mode::Normal;
                emitModeIfChanged(result, before);
                return;
            }
            baseMode = Mode::Normal;
            finishInsertOneNormal(result, viewId);
            return;
        }
        const auto foundView = views.find(viewId);
        if ((detail::isSpecialKey(
                 key, SpecialKey::Escape)
             || controlCancel)
            && foundView != views.end()
            && foundView->second.kind
                == ViewKind::Surface) {
            emitHost(result, HostAction::Cancel);
        }
        const Mode before = effectiveMode();
        if (before == Mode::Replace) {
            input.clear();
            keywordCompletion.reset();
            pendingOperator.reset();
            pendingArgument.reset();
            normalCount = 0;
            prefix = CommandPrefix::None;
            selectedRegister = U'"';
            finishReplaceMode(&result, viewId);
            return;
        }
        const bool retreatInsertCursor =
            before == Mode::Insert
            && activeInsertRepeat
            && (!activeInsertRepeat->insertedText.empty()
                || activeInsertRepeat->command
                    == Command::AppendInsert);
        if (before == Mode::Insert) {
            finishInsertRepeat(&result);
            closeInsertUndoBlock();
        }
        if (before == Mode::Visual) {
            leaveVisualMode();
        }
        input.clear();
        keywordCompletion.reset();
        pendingOperator.reset();
        pendingArgument.reset();
        normalCount = 0;
        prefix = CommandPrefix::None;
        selectedRegister = U'"';
        baseMode = Mode::Normal;
        const auto active = views.find(viewId);
        if (before == Mode::Insert
            && active != views.end()
            && active->second.buffer != 0) {
            const auto activeBuffer =
                buffers.find(active->second.buffer);
            if (activeBuffer != buffers.end()) {
                Cursor &cursor =
                    active->second.cursors[
                        active->second.buffer];
                cursor = clampCursor(
                    activeBuffer->second,
                    cursor,
                    true);
                const std::size_t insertExitOffset =
                    offset(activeBuffer->second, cursor);
                if (retreatInsertCursor
                    && cursor.column > 0) {
                    cursor.column = previousColumnAllowEnd(
                        activeBuffer->second,
                        cursor.line,
                        cursor.column);
                }
                cursor = clampCursor(
                    activeBuffer->second,
                    cursor,
                    false);
                activeBuffer->second.lastInsertExitMark =
                    insertExitOffset;
                active->second.preferredColumn.reset();
                emitCursor(
                    result, viewId, active->second);
            }
        }
        emitModeIfChanged(result, before);
        return;
    }
    if (insertOneNormalPending) {
        insertOneNormalSawKey = true;
    }
    if (consumePendingArgument(
            result, viewId, key)) {
        return;
    }
    if (baseMode == Mode::Replace) {
        if (key.modifiers == KeyModifier::None) {
            if (const auto scalar = detail::character(key)) {
                std::u16string inserted;
                appendUtf16(inserted, *scalar);
                (void)replaceOverwrite(
                    result,
                    viewId,
                    std::move(inserted),
                    true);
                return;
            }
            if (detail::isSpecialKey(
                    key, SpecialKey::Enter)) {
                (void)replaceOverwrite(
                    result, viewId, u"\n", true);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Tab)) {
                (void)replaceOverwrite(
                    result, viewId, u"\t", true);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Backspace)) {
                (void)replaceBackspace(result, viewId);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Delete)) {
                (void)replaceDelete(
                    result, viewId, true);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Left)) {
                moveReplaceCursor(
                    result, viewId, SpecialKey::Left);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Right)) {
                moveReplaceCursor(
                    result, viewId, SpecialKey::Right);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Up)) {
                moveReplaceCursor(
                    result, viewId, SpecialKey::Up);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Down)) {
                moveReplaceCursor(
                    result, viewId, SpecialKey::Down);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::PageUp)) {
                moveReplaceCursor(
                    result, viewId, SpecialKey::PageUp);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::PageDown)) {
                moveReplaceCursor(
                    result, viewId, SpecialKey::PageDown);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Home)) {
                moveReplaceCursor(
                    result, viewId, SpecialKey::Home);
            } else if (detail::isSpecialKey(
                           key, SpecialKey::End)) {
                moveReplaceCursor(
                    result, viewId, SpecialKey::End);
            }
        }
        return;
    }
    if (baseMode == Mode::Insert) {
        if (const auto *const insertCommand =
                findNativeDescriptor(
                    kInsertCommandGrammar, key);
            insertCommand != nullptr) {
            keywordCompletion.reset();
            if (insertCommand->kind
                == NativeInsertCommandKind::
                    BeginRegisterInsert) {
                const auto found = views.find(viewId);
                if (found != views.end()
                    && found->second.buffer != 0) {
                    pendingArgument = PendingArgument{
                        PendingArgumentKind::InsertRegister,
                        viewId,
                        found->second.buffer,
                        1,
                        false};
                }
            } else if (insertCommand->kind
                       == NativeInsertCommandKind::
                           ExecuteOneNormal) {
                beginInsertOneNormal(result, viewId);
            } else {
                (void)deleteInsertPrefix(
                    result,
                    viewId,
                    insertCommand->kind
                        == NativeInsertCommandKind::
                            DeleteToLineStart);
            }
            return;
        }
        if (isInsertKeywordCompletionKey(key)) {
            (void)completeKeyword(
                result,
                viewId,
                modifiedCharacter(
                    key,
                    U'n',
                    KeyModifier::Control));
            return;
        }
        keywordCompletion.reset();
        Event event;
        event.view = viewId;
        event.inputTarget = inputTarget;
        if (key.modifiers == KeyModifier::Super) {
            const auto scalar = detail::character(key);
            if (!scalar) {
                return;
            }
            event.type = EventType::InsertCommand;
            if (*scalar == U'c') {
                event.insertCommand = InsertCommand::Copy;
            } else if (*scalar == U'x') {
                event.insertCommand = InsertCommand::Cut;
            } else if (*scalar == U'v') {
                event.insertCommand = InsertCommand::Paste;
            } else if (*scalar == U'a') {
                event.insertCommand = InsertCommand::SelectAll;
            } else {
                return;
            }
            result.events.push_back(std::move(event));
            return;
        }
        if (key.modifiers == KeyModifier::None) {
            if (isInsertNavigationKey(key)) {
                // Cursor movement is an Insert-mode undo boundary in
                // Neovim. The following host edit lazily opens a fresh
                // node through applyExternalEdit().
                closeInsertUndoBlock();
            }
            if (const auto scalar = detail::character(key)) {
                event.type = EventType::InsertText;
                appendUtf16(event.editInserted, *scalar);
                result.events.push_back(std::move(event));
                return;
            }
            if (detail::isSpecialKey(
                    key, SpecialKey::Enter)) {
                event.type = EventType::InsertText;
                event.editInserted = u"\n";
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Tab)) {
                event.type = EventType::InsertText;
                event.editInserted = u"\t";
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Backspace)) {
                event.type = EventType::InsertCommand;
                event.insertCommand =
                    InsertCommand::Backspace;
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Delete)) {
                event.type = EventType::InsertCommand;
                event.insertCommand =
                    InsertCommand::Delete;
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Left)) {
                event.type = EventType::InsertCommand;
                event.insertCommand =
                    InsertCommand::MoveLeft;
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Right)) {
                event.type = EventType::InsertCommand;
                event.insertCommand =
                    InsertCommand::MoveRight;
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Up)) {
                event.type = EventType::InsertCommand;
                event.insertCommand =
                    InsertCommand::MoveUp;
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Down)) {
                event.type = EventType::InsertCommand;
                event.insertCommand =
                    InsertCommand::MoveDown;
            } else if (detail::isSpecialKey(
                           key, SpecialKey::PageUp)) {
                event.type = EventType::InsertCommand;
                event.insertCommand =
                    InsertCommand::PageUp;
            } else if (detail::isSpecialKey(
                           key, SpecialKey::PageDown)) {
                event.type = EventType::InsertCommand;
                event.insertCommand =
                    InsertCommand::PageDown;
            } else if (detail::isSpecialKey(
                           key, SpecialKey::Home)) {
                event.type = EventType::InsertCommand;
                event.insertCommand =
                    InsertCommand::MoveToStart;
            } else if (detail::isSpecialKey(
                           key, SpecialKey::End)) {
                event.type = EventType::InsertCommand;
                event.insertCommand =
                    InsertCommand::MoveToEnd;
            } else {
                return;
            }
            result.events.push_back(std::move(event));
        }
        return;
    }

    if (baseMode == Mode::Visual) {
        const auto visualView = views.find(viewId);
        const bool visualBlock =
            visualView != views.end()
            && ownsVisualBlock(
                viewId, visualView->second.buffer);
        if (prefix == CommandPrefix::G
            && (plainCharacter(key, U'u')
                || plainCharacter(key, U'U'))) {
            prefix = CommandPrefix::None;
            const std::size_t count =
                std::max<std::size_t>(1, normalCount);
            normalCount = 0;
            (void)finishVisualTransform(
                result,
                viewId,
                plainCharacter(key, U'u')
                    ? OperatorKind::Lowercase
                    : OperatorKind::Uppercase,
                count,
                true);
            return;
        }
        if (plainCharacter(key, U'u')
            || plainCharacter(key, U'U')) {
            const std::size_t count =
                std::max<std::size_t>(1, normalCount);
            normalCount = 0;
            (void)finishVisualTransform(
                result,
                viewId,
                plainCharacter(key, U'u')
                    ? OperatorKind::Lowercase
                    : OperatorKind::Uppercase,
                count,
                true);
            return;
        }
        if (plainCharacter(key, U'>')
            || plainCharacter(key, U'<')) {
            const std::size_t count =
                std::max<std::size_t>(1, normalCount);
            normalCount = 0;
            (void)finishVisualTransform(
                result,
                viewId,
                plainCharacter(key, U'>')
                    ? OperatorKind::ShiftRight
                    : OperatorKind::ShiftLeft,
                count,
                true);
            return;
        }
        if (visualBlock
            && (plainCharacter(key, U'I')
                || plainCharacter(key, U'A'))) {
            (void)beginVisualBlockInsert(
                result,
                viewId,
                plainCharacter(key, U'A'));
            return;
        }
        if (visualBlock
            && (plainCharacter(key, U'o')
                || plainCharacter(key, U'O'))) {
            (void)swapVisualBlockCorner(
                result,
                viewId,
                plainCharacter(key, U'O'));
            return;
        }
        if (visualBlock
            && (plainCharacter(key, U'p')
                || plainCharacter(key, U'P'))) {
            const RegisterValue payload =
                registerValue(selectedRegister);
            selectedRegister = U'"';
            (void)putOverVisualBlock(
                result,
                viewId,
                payload,
                plainCharacter(key, U'p')
                    ? Command::PutAfter
                    : Command::PutBefore);
            return;
        }
        if (visualBlock
            && plainCharacter(key, U'~')) {
            (void)toggleVisualBlock(
                result, viewId, true);
            return;
        }
        if (plainCharacter(key, U'i')
            || plainCharacter(key, U'a')) {
            if (visualView != views.end()
                && visualView->second.buffer != 0) {
                pendingArgument = PendingArgument{
                    PendingArgumentKind::TextObject,
                    viewId,
                    visualView->second.buffer,
                    std::max<std::size_t>(
                        1, normalCount),
                    normalCount != 0,
                    true,
                    false,
                    plainCharacter(key, U'a')};
                normalCount = 0;
            }
            return;
        }
        if (plainCharacter(key, U'd')
            || plainCharacter(key, U'x')) {
            finishVisualOperation(
                result,
                viewId,
                OperatorKind::Delete);
            return;
        }
        if (plainCharacter(key, U'c')
            || plainCharacter(key, U's')) {
            finishVisualOperation(
                result,
                viewId,
                OperatorKind::Change);
            return;
        }
        if (plainCharacter(key, U'y')) {
            finishVisualOperation(
                result,
                viewId,
                OperatorKind::Yank);
            return;
        }
    }

    // Operator counts are read before motion dispatch. A zero continues
    // an existing count (d50%), while a leading zero remains the d0
    // line-start motion. This mirrors Neovim's normal_cmd_get_count().
    if (pendingOperator
        && prefix == CommandPrefix::None) {
        const auto value = detail::character(key);
        if (key.modifiers == KeyModifier::None
            && value
            && *value >= U'0'
            && *value <= U'9') {
            const std::size_t digit =
                static_cast<std::size_t>(*value - U'0');
            if (digit != 0 || normalCount != 0) {
                normalCount = std::min<std::size_t>(
                    999'999'999,
                    normalCount * 10 + digit);
                return;
            }
        }
    }

    if (pendingOperator) {
        if (prefix == CommandPrefix::G) {
            prefix = CommandPrefix::None;
            if (const auto *const descriptor =
                    findNativeDescriptor(
                        kGotoGrammar, key);
                descriptor != nullptr
                && (descriptor->command
                        == Command::FirstLine
                    || descriptor->command
                        == Command::LastNonBlank
                    || descriptor->command
                        == Command::WordEndBackward
                    || descriptor->command
                        == Command::BigWordEndBackward
                    || descriptor->command
                        == Command::TextMiddle
                    || displayMotion(
                        descriptor->command))) {
                if (displayMotion(
                        descriptor->command)) {
                    requestPendingOperatorDisplayMotion(
                        result,
                        viewId,
                        descriptor->command);
                } else {
                    finishMotionOperator(
                        result,
                        viewId,
                        descriptor->command);
                }
            } else {
                const Mode before = effectiveMode();
                pendingOperator.reset();
                normalCount = 0;
                emitModeIfChanged(result, before);
            }
            return;
        }
        if (prefix == CommandPrefix::BracketLeft
            || prefix == CommandPrefix::BracketRight) {
            const CommandPrefix activePrefix = prefix;
            prefix = CommandPrefix::None;
            const NativeCommandDescriptor *descriptor =
                activePrefix == CommandPrefix::BracketLeft
                ? findNativeDescriptor(
                      kBracketLeftGrammar, key)
                : findNativeDescriptor(
                      kBracketRightGrammar, key);
            const bool sectionMotion =
                descriptor != nullptr
                && (descriptor->command
                        == Command::SectionBackwardStart
                    || descriptor->command
                        == Command::SectionForwardStart
                    || descriptor->command
                        == Command::SectionBackwardEnd
                    || descriptor->command
                        == Command::SectionForwardEnd);
            if (sectionMotion) {
                finishMotionOperator(
                    result,
                    viewId,
                    descriptor->command);
            } else {
                const Mode before = effectiveMode();
                pendingOperator.reset();
                normalCount = 0;
                emitModeIfChanged(result, before);
            }
            return;
        }
        const auto *const repeatedOperator =
            findNativeDescriptor(kOperators, key);
        const bool repeatedCaseOperator =
            (pendingOperator->kind
                 == OperatorKind::Lowercase
             && plainCharacter(key, U'u'))
            || (pendingOperator->kind
                    == OperatorKind::Uppercase
                && plainCharacter(key, U'U'));
        if ((repeatedOperator != nullptr
             && repeatedOperator->kind
                 == pendingOperator->kind)
            || repeatedCaseOperator) {
            finishLinewiseOperator(result, viewId);
            return;
        }
        const auto *const continuation =
            findNativeDescriptor(
                kOperatorContinuations, key);
        if (continuation != nullptr
            && continuation->kind
                == OperatorContinuationKind::
                    GotoPrefix) {
            prefix = CommandPrefix::G;
            return;
        }
        if (continuation != nullptr
            && continuation->kind
                == OperatorContinuationKind::
                    BracketLeftPrefix) {
            prefix = CommandPrefix::BracketLeft;
            return;
        }
        if (continuation != nullptr
            && continuation->kind
                == OperatorContinuationKind::
                    BracketRightPrefix) {
            prefix = CommandPrefix::BracketRight;
            return;
        }
        if (continuation != nullptr
            && continuation->kind
                == OperatorContinuationKind::
                    TextObjectPrefix) {
            const std::size_t motionMultiplier =
                std::max<std::size_t>(
                    1, normalCount);
            const std::size_t objectCount =
                pendingOperator->count
                    > 999'999'999
                        / motionMultiplier
                ? 999'999'999
                : pendingOperator->count
                    * motionMultiplier;
            const bool around =
                plainCharacter(key, U'a');
            normalCount = 0;
            pendingArgument = PendingArgument{
                PendingArgumentKind::TextObject,
                viewId,
                pendingOperator->buffer,
                objectCount,
                pendingOperator->countWasExplicit
                    || motionMultiplier != 1,
                true,
                false,
                around};
            return;
        }
        if (continuation != nullptr
            && continuation->kind
                == OperatorContinuationKind::
                    CharacterSearch) {
            const std::size_t motionMultiplier =
                std::max<std::size_t>(
                    1, normalCount);
            const std::size_t motionCount =
                pendingOperator->count
                    > 999'999'999
                        / motionMultiplier
                ? 999'999'999
                : pendingOperator->count
                    * motionMultiplier;
            const bool explicitCount =
                pendingOperator->countWasExplicit
                || normalCount != 0;
            normalCount = 0;
            const bool forward =
                continuation->command
                    == Command::BeginFindForward
                || continuation->command
                    == Command::BeginTillForward;
            const bool till =
                continuation->command
                    == Command::BeginTillForward
                || continuation->command
                    == Command::BeginTillBackward;
            beginCharacterSearchArgument(
                viewId,
                motionCount,
                explicitCount,
                forward,
                till);
            return;
        }
        if (continuation != nullptr
            && continuation->kind
                == OperatorContinuationKind::
                    RepeatCharacterSearch) {
            const std::size_t motionMultiplier =
                std::max<std::size_t>(
                    1, normalCount);
            const std::size_t motionCount =
                pendingOperator->count
                    > 999'999'999
                        / motionMultiplier
                ? 999'999'999
                : pendingOperator->count
                    * motionMultiplier;
            normalCount = 0;
            applyRepeatedCharacterSearch(
                result,
                viewId,
                motionCount,
                continuation->command
                    == Command::
                        RepeatCharacterSearchOpposite);
            return;
        }
        if (continuation != nullptr
            && continuation->kind
                == OperatorContinuationKind::
                    MatchingPair) {
            const bool percentageMotion =
                pendingOperator->countWasExplicit
                || normalCount != 0;
            if (percentageMotion) {
                finishMotionOperator(
                    result,
                    viewId,
                    Command::FilePercent);
            } else {
                normalCount = 0;
                applyMatchingPairMotion(
                    result, viewId);
            }
            return;
        }
        if (continuation != nullptr
            && continuation->kind
                == OperatorContinuationKind::Motion) {
            finishMotionOperator(
                result,
                viewId,
                continuation->command);
            return;
        }
    }

    if (const auto value = detail::character(key);
        key.modifiers == KeyModifier::None
        && value
        && *value >= U'0'
        && *value <= U'9'
        && prefix == CommandPrefix::None) {
        const std::size_t digit =
            static_cast<std::size_t>(*value - U'0');
        if (digit != 0 || normalCount != 0) {
            normalCount = std::min<std::size_t>(
                999'999'999,
                normalCount * 10 + digit);
            return;
        }
    }
    if (pendingOperator) {
        const Mode before = effectiveMode();
        pendingOperator.reset();
        normalCount = 0;
        prefix = CommandPrefix::None;
        emitModeIfChanged(result, before);
        return;
    }

    if (prefix != CommandPrefix::None) {
        const CommandPrefix activePrefix = prefix;
        prefix = CommandPrefix::None;
        const auto executeCommandGrammar =
            [this, &result, viewId, &key](
                const auto &grammar) {
                const auto *const descriptor =
                    findNativeDescriptor(
                        grammar, key);
                if (descriptor == nullptr) {
                    return false;
                }
                execute(
                    result,
                    viewId,
                    descriptor->command);
                return true;
            };
        if (activePrefix == CommandPrefix::G) {
            (void)executeCommandGrammar(
                kGotoGrammar);
        } else if (
            activePrefix
            == CommandPrefix::BracketLeft) {
            (void)executeCommandGrammar(
                kBracketLeftGrammar);
        } else if (
            activePrefix
            == CommandPrefix::BracketRight) {
            (void)executeCommandGrammar(
                kBracketRightGrammar);
        } else if (
            activePrefix == CommandPrefix::Window) {
            if (const auto *const descriptor =
                    findWindowDescriptor(key)) {
                consumeSemanticCommand(
                    result,
                    std::string(descriptor->commandId),
                    inputTarget);
            } else {
                Event error;
                error.type = EventType::InputError;
                error.view = viewId;
                error.message =
                    "unsupported CTRL-W command";
                result.events.push_back(std::move(error));
            }
        }
        normalCount = 0;
        return;
    }

    if (const auto *const starter =
            findNativePrefixDescriptor(
                kPrefixStarters, key)) {
        prefix = starter->prefix;
        return;
    }
    if (const auto *const operation =
            findNativeDescriptor(
                kOperators, key)) {
        beginOperator(
            result,
            viewId,
            operation->kind);
        return;
    }

    if (const auto command = normalCommand(key)) {
        execute(result, viewId, *command);
        return;
    }

    if (baseMode == Mode::Normal
        && detail::isSpecialKey(
            key, SpecialKey::Tab)) {
        // In Normal mode <Tab> is CTRL-I's native jumplist alias. Insert
        // and Replace consumed their literal tab paths above; Visual is
        // deliberately excluded so this alias cannot change selection
        // state or leave Visual mode.
        execute(
            result,
            viewId,
            Command::JumpListForward);
    } else if (detail::isSpecialKey(
            key, SpecialKey::Left)
        || detail::isSpecialKey(
            key, SpecialKey::Backspace)) {
        execute(result, viewId, Command::MoveLeft);
    } else if (detail::isSpecialKey(
            key, SpecialKey::Down)) {
        execute(result, viewId, Command::MoveDown);
    } else if (detail::isSpecialKey(
            key, SpecialKey::Up)) {
        execute(result, viewId, Command::MoveUp);
    } else if (detail::isSpecialKey(
                   key, SpecialKey::Enter)) {
        const auto active = views.find(viewId);
        if (active != views.end()
            && (active->second.kind
                    == ViewKind::Navigation
                || active->second.kind
                    == ViewKind::Surface)) {
            emitHost(result, HostAction::Activate);
        } else {
            execute(
                result,
                viewId,
                Command::LineDownFirstNonBlank);
        }
    } else if (detail::isSpecialKey(
                   key, SpecialKey::PageDown)) {
        const auto active = views.find(viewId);
        execute(
            result,
            viewId,
            active != views.end()
                    && (active->second.kind
                            == ViewKind::Navigation
                        || active->second.kind
                            == ViewKind::Surface)
                ? Command::MoveDown
                : Command::ScrollPageDown);
    } else if (detail::isSpecialKey(
                   key, SpecialKey::PageUp)) {
        const auto active = views.find(viewId);
        execute(
            result,
            viewId,
            active != views.end()
                    && (active->second.kind
                            == ViewKind::Navigation
                        || active->second.kind
                            == ViewKind::Surface)
                ? Command::MoveUp
                : Command::ScrollPageUp);
    } else if (detail::isSpecialKey(
                   key, SpecialKey::Right)) {
        execute(result, viewId, Command::MoveRight);
    } else if (detail::isSpecialKey(
                   key, SpecialKey::Home)) {
        execute(result, viewId, Command::LineStart);
    } else if (detail::isSpecialKey(
                   key, SpecialKey::End)) {
        execute(result, viewId, Command::LineEnd);
    } else {
        normalCount = 0;
    }
}

[[nodiscard]] bool VkCore::Implementation::nativeGrammarPending() const noexcept
{
    return prefix != CommandPrefix::None
        || pendingOperator.has_value()
        || pendingArgument.has_value()
        || (insertOneNormalPending
            && (!insertOneNormalSawKey
                || normalCount != 0));
}

[[nodiscard]] std::u32string
VkCore::Implementation::pendingOperatorNotation() const
{
    if (!pendingOperator) {
        return {};
    }
    if (pendingOperator->kind == OperatorKind::Lowercase) {
        return U"gu";
    }
    if (pendingOperator->kind == OperatorKind::Uppercase) {
        return U"gU";
    }
    const auto found = std::ranges::find_if(
        kOperators,
        [this](
            const NativeOperatorDescriptor
                &descriptor) {
            return descriptor.kind
                == pendingOperator->kind;
        });
    return found == kOperators.cend()
        ? std::u32string{}
        : std::u32string(found->hint.key.notation);
}

void VkCore::Implementation::appendNativeHint(
    InputHintSnapshot &snapshot,
    const NativeHintDescriptor &descriptor,
    const std::string_view descriptionOverride,
    const std::string_view groupOverride)
{
    std::u32string key(descriptor.key.notation);
    const std::u32string sequence =
        snapshot.prefixNotation + key;
    snapshot.candidates.push_back(
        InputHintCandidate{
            std::move(key),
            sequence,
            0,
            descriptor.completes,
            descriptor.hasChildren,
            false,
            false,
            InputHintOrigin::NativeGrammar,
            std::string(
                descriptionOverride.empty()
                    ? descriptor.description
                    : descriptionOverride),
            std::string(
                groupOverride.empty()
                    ? descriptor.group
                    : groupOverride),
            {},
            false,
            descriptor.order,
            {}});
}

template<typename Descriptor, std::size_t Size>
void VkCore::Implementation::appendNativeHints(
    InputHintSnapshot &snapshot,
    const std::array<Descriptor, Size> &descriptors)
{
    for (const Descriptor &descriptor : descriptors) {
        appendNativeHint(
            snapshot, descriptor.hint);
        if constexpr (requires {
                          descriptor.commandId;
                      }) {
            snapshot.candidates.back().commandId =
                std::string(descriptor.commandId);
        }
    }
}

[[nodiscard]] std::optional<InputHintSnapshot>
VkCore::Implementation::nativeGrammarSnapshot(
    const std::uint64_t generation) const
{
    if (!nativeGrammarPending()
        || pendingView == 0) {
        return std::nullopt;
    }
    const auto foundView = views.find(pendingView);
    if (foundView == views.end()) {
        return std::nullopt;
    }

    InputHintSnapshot snapshot;
    snapshot.mode =
        mappingModeFor(effectiveMode());
    snapshot.buffer = foundView->second.buffer == 0
        ? std::nullopt
        : std::optional<BufferId>(
              foundView->second.buffer);
    snapshot.window = pendingView;
    snapshot.generation = generation;
    snapshot.waitPolicy =
        InputHintWaitPolicy::PersistentGrammar;

    if (insertOneNormalPending
        && !pendingArgument
        && !pendingOperator
        && prefix == CommandPrefix::None) {
        snapshot.prefixNotation = U"<C-o>";
        if (normalCount != 0) {
            const std::string count =
                std::to_string(normalCount);
            for (const char digit : count) {
                snapshot.prefixNotation.push_back(
                    static_cast<char32_t>(digit));
            }
        }
        appendNativeHint(
            snapshot, kOneNormalCommandHint);
        return snapshot;
    }

    if (pendingArgument) {
        const std::u32string activeOperator =
            pendingOperatorNotation();
        switch (pendingArgument->kind) {
        case PendingArgumentKind::ReplaceCharacter:
            snapshot.prefixNotation = U"r";
            appendNativeHints(
                snapshot, kReplacementGrammar);
            break;
        case PendingArgumentKind::CharacterSearch:
            if (!activeOperator.empty()) {
                snapshot.prefixNotation +=
                    activeOperator;
            }
            snapshot.prefixNotation +=
                pendingArgument->searchTill
                ? (pendingArgument->searchForward
                       ? U"t"
                       : U"T")
                : (pendingArgument->searchForward
                       ? U"f"
                       : U"F");
            appendNativeHint(
                snapshot,
                kCharacterArgumentHint,
                pendingArgument->searchTill
                    ? "Move before character"
                    : "Move to character",
                "Character");
            break;
        case PendingArgumentKind::ViewportPosition:
            snapshot.prefixNotation = U"z";
            appendNativeHints(
                snapshot, kViewportGrammar);
            break;
        case PendingArgumentKind::SelectRegister:
            snapshot.prefixNotation = U"\"";
            appendNativeHints(
                snapshot, kRegisterGrammar);
            break;
        case PendingArgumentKind::InsertRegister:
            snapshot.prefixNotation = U"<C-r>";
            appendNativeHints(
                snapshot, kRegisterGrammar);
            break;
        case PendingArgumentKind::SetMark:
            snapshot.prefixNotation = U"m";
            appendNativeHints(
                snapshot, kMarkGrammar);
            break;
        case PendingArgumentKind::JumpMarkLine:
            snapshot.prefixNotation = U"'";
            appendNativeHints(
                snapshot, kJumpMarkGrammar);
            break;
        case PendingArgumentKind::JumpMarkExact:
            snapshot.prefixNotation = U"`";
            appendNativeHints(
                snapshot, kJumpMarkGrammar);
            break;
        case PendingArgumentKind::TextObject:
            if (!activeOperator.empty()) {
                snapshot.prefixNotation +=
                    activeOperator;
            }
            snapshot.prefixNotation.push_back(
                pendingArgument->around
                    ? U'a'
                    : U'i');
            appendNativeHints(
                snapshot, kTextObjectGrammar);
            break;
        case PendingArgumentKind::MacroRecord:
            snapshot.prefixNotation = U"q";
            appendNativeHints(
                snapshot, kMacroRegisterGrammar);
            if (!snapshot.candidates.empty()) {
                snapshot.candidates.pop_back();
            }
            break;
        case PendingArgumentKind::MacroExecute:
            snapshot.prefixNotation = U"@";
            appendNativeHints(
                snapshot, kMacroRegisterGrammar);
            break;
        }
        return snapshot;
    }

    const std::u32string activeOperator =
        pendingOperatorNotation();

    if (prefix == CommandPrefix::G) {
        if (!activeOperator.empty()) {
            snapshot.prefixNotation +=
                activeOperator;
        }
        snapshot.prefixNotation.push_back(U'g');
        appendNativeHints(
            snapshot, kGotoGrammar);
        return snapshot;
    }

    if (prefix == CommandPrefix::BracketLeft) {
        if (!activeOperator.empty()) {
            snapshot.prefixNotation += activeOperator;
        }
        snapshot.prefixNotation.push_back(U'[');
        appendNativeHints(
            snapshot, kBracketLeftGrammar);
        return snapshot;
    }

    if (prefix == CommandPrefix::BracketRight) {
        if (!activeOperator.empty()) {
            snapshot.prefixNotation += activeOperator;
        }
        snapshot.prefixNotation.push_back(U']');
        appendNativeHints(
            snapshot, kBracketRightGrammar);
        return snapshot;
    }

    if (prefix == CommandPrefix::Window) {
        snapshot.prefixNotation = U"<C-w>";
        appendNativeHints(
            snapshot, kWindowGrammar);
        return snapshot;
    }

    if (activeOperator.empty()) {
        return std::nullopt;
    }
    snapshot.prefixNotation = activeOperator;
    const auto operation =
        std::ranges::find_if(
            kOperators,
            [this](
                const NativeOperatorDescriptor
                    &descriptor) {
                return pendingOperator
                    && descriptor.kind
                        == pendingOperator->kind;
            });
    if (operation != kOperators.cend()) {
        appendNativeHint(
            snapshot, operation->hint);
    } else if (pendingOperator
               && pendingOperator->kind
                   == OperatorKind::Lowercase) {
        constexpr NativeHintDescriptor lowerLine{
            characterKey(U'u', U"u"),
            "Lowercase line",
            "Operator",
            true,
            false,
            10};
        appendNativeHint(snapshot, lowerLine);
    } else if (pendingOperator
               && pendingOperator->kind
                   == OperatorKind::Uppercase) {
        constexpr NativeHintDescriptor upperLine{
            characterKey(U'U', U"U"),
            "Uppercase line",
            "Operator",
            true,
            false,
            10};
        appendNativeHint(snapshot, upperLine);
    }
    appendNativeHints(
        snapshot, kOperatorContinuations);
    return snapshot;
}

void VkCore::Implementation::mergeNativeFallbackHints(
    InputHintSnapshot &snapshot) const
{
    if (snapshot.waitPolicy
            == InputHintWaitPolicy::PersistentLeader
        || !snapshot.nativeFallbackReachable
        || snapshot.prefixNotation.empty()) {
        return;
    }

    InputHintSnapshot native = snapshot;
    native.candidates.clear();
    const std::u32string_view prefixNotation =
        snapshot.prefixNotation;

    if (prefixNotation == U"g") {
        appendNativeHints(native, kGotoGrammar);
    } else if (prefixNotation == U"[") {
        appendNativeHints(
            native, kBracketLeftGrammar);
    } else if (prefixNotation == U"]") {
        appendNativeHints(
            native, kBracketRightGrammar);
    } else if (prefixNotation == U"<C-w>") {
        appendNativeHints(native, kWindowGrammar);
    } else if (prefixNotation == U"z") {
        appendNativeHints(native, kViewportGrammar);
    } else if (prefixNotation == U"\"") {
        appendNativeHints(native, kRegisterGrammar);
    } else if (prefixNotation == U"m") {
        appendNativeHints(native, kMarkGrammar);
    } else if (
        prefixNotation == U"'"
        || prefixNotation == U"`") {
        appendNativeHints(native, kJumpMarkGrammar);
    } else if (prefixNotation == U"q") {
        appendNativeHints(
            native, kMacroRegisterGrammar);
        if (!native.candidates.empty()) {
            native.candidates.pop_back();
        }
    } else if (prefixNotation == U"@") {
        appendNativeHints(
            native, kMacroRegisterGrammar);
    } else if (prefixNotation == U"r") {
        appendNativeHints(
            native, kReplacementGrammar);
    } else if (
        prefixNotation == U"f"
        || prefixNotation == U"F"
        || prefixNotation == U"t"
        || prefixNotation == U"T") {
        appendNativeHint(
            native,
            kCharacterArgumentHint,
            prefixNotation == U"t"
                    || prefixNotation == U"T"
                ? "Move before character"
                : "Move to character",
            "Character");
    } else {
        const auto operation = std::ranges::find_if(
            kOperators,
            [prefixNotation](
                const NativeOperatorDescriptor
                    &descriptor) {
                return descriptor.hint.key.notation
                    == prefixNotation;
            });
        if (operation != kOperators.cend()) {
            appendNativeHint(
                native, operation->hint);
            appendNativeHints(
                native, kOperatorContinuations);
        } else if (
            prefixNotation.size() == 2
            && (prefixNotation.front() == U'd'
                || prefixNotation.front() == U'c'
                || prefixNotation.front() == U'y')) {
            const char32_t continuation =
                prefixNotation.back();
            if (continuation == U'g') {
                appendNativeHints(
                    native, kGotoGrammar);
            } else if (
                continuation == U'f'
                || continuation == U'F'
                || continuation == U't'
                || continuation == U'T') {
                appendNativeHint(
                    native,
                    kCharacterArgumentHint,
                    continuation == U't'
                            || continuation == U'T'
                        ? "Move before character"
                        : "Move to character",
                    "Character");
            } else if (
                continuation == U'i'
                || continuation == U'a') {
                appendNativeHints(
                    native, kTextObjectGrammar);
            }
        }
    }

    for (InputHintCandidate &candidate :
         native.candidates) {
        const bool shadowed =
            std::ranges::any_of(
                snapshot.candidates,
                [&candidate](
                    const InputHintCandidate
                        &mapping) {
                    return mapping.keyNotation
                        == candidate.keyNotation;
                });
        if (!shadowed) {
            snapshot.candidates.push_back(
                std::move(candidate));
        }
    }
    std::ranges::stable_sort(
        snapshot.candidates,
        [](const InputHintCandidate &lhs,
           const InputHintCandidate &rhs) {
            const auto locality = [](
                const InputHintCandidate &candidate) {
                if (candidate.windowLocal) {
                    return 0;
                }
                if (candidate.bufferLocal) {
                    return 1;
                }
                return candidate.origin
                        == InputHintOrigin::Mapping
                    ? 2
                    : 3;
            };
            if (locality(lhs) != locality(rhs)) {
                return locality(lhs) < locality(rhs);
            }
            if (lhs.order != rhs.order) {
                return lhs.order < rhs.order;
            }
            if (lhs.keyNotation != rhs.keyNotation) {
                return lhs.keyNotation
                    < rhs.keyNotation;
            }
            // This tie is normally removed above. Keep mapping first as a
            // defensive invariant if notation aliases are added later.
            return lhs.origin
                == InputHintOrigin::Mapping
                && rhs.origin
                    == InputHintOrigin::NativeGrammar;
        });
}

[[nodiscard]] bool VkCore::Implementation::inputHintSessionIsLive() const noexcept
{
    return enabled
        && !pendingHostBarrierGeneration
        && inputHintSession.has_value()
        && pendingGeneration.has_value()
        && *pendingGeneration
            == inputHintSession->generation
        && pendingView != 0
        && views.contains(pendingView);
}

void VkCore::Implementation::endInputHintPresentation()
{
    if (!inputHintSession) {
        return;
    }
    const bool detachedRoot =
        inputHintSession->atRoot
        && pendingGeneration
        && *pendingGeneration
            == inputHintSession->generation;
    inputHintSession.reset();
    if (!detachedRoot) {
        return;
    }
    pendingGeneration.reset();
    pendingView = 0;
    pendingTarget = 0;
    pendingHintPolicy.reset();
    ++inputGeneration;
}

void VkCore::Implementation::retreatNativeHintPrefix()
{
    if (pendingArgument) {
        pendingArgument.reset();
        return;
    }
    if (prefix != CommandPrefix::None) {
        prefix = CommandPrefix::None;
        return;
    }
    if (pendingOperator) {
        pendingOperator.reset();
        normalCount = 0;
        return;
    }
    // A detached root and the one-Normal-command root both have no
    // additional native parent that which-key can present.
    normalCount = 0;
}

[[nodiscard]] std::optional<DispatchResult>
VkCore::Implementation::consumeInputHintSessionKey(
    const ViewId view,
    const InputTargetId inputTarget,
    const KeyAtom &key)
{
    if (!inputHintSessionIsLive()) {
        inputHintSession.reset();
        return std::nullopt;
    }

    const bool escape = detail::isSpecialKey(
                            key, SpecialKey::Escape)
        || modifiedCharacter(
            key, U'[', KeyModifier::Control);
    const bool backspace = detail::isSpecialKey(
        key, SpecialKey::Backspace);
    const bool pageDown = modifiedCharacter(
        key, U'd', KeyModifier::Control);
    const bool pageUp = modifiedCharacter(
        key, U'u', KeyModifier::Control);

    if (!escape && !backspace
        && !pageDown && !pageUp) {
        // The next ordinary key belongs to the real resolver. End only
        // the UI lease; dispatch() below will append and resolve the same
        // physical canonical key exactly once.
        endInputHintPresentation();
        return std::nullopt;
    }

    DispatchResult result;
    result.disposition = InputDisposition::Consumed;
    if (escape) {
        const Mode before = effectiveMode();
        clearPendingState();
        emitModeIfChanged(result, before);
        return result;
    }

    if (pageDown || pageUp) {
        const InputHintSession &session =
            *inputHintSession;
        Event event;
        event.type = EventType::CommandRequested;
        event.view = view;
        event.inputTarget = inputTarget;
        const auto foundView = views.find(view);
        if (foundView != views.end()) {
            event.buffer = foundView->second.buffer;
        }
        event.mode = effectiveMode();
        event.commandId = pageDown
            ? session.pageDownCommand
            : session.pageUpCommand;
        event.count = 1;
        event.countWasExplicit = false;
        result.events.push_back(std::move(event));
        result.disposition = InputDisposition::Pending;
        result.inputHintGeneration =
            session.generation;
        result.mappingInputTarget = pendingTarget;
        if (pendingHintPolicy
                == InputHintWaitPolicy::TimedMapping) {
            // Paging is interaction with the visible discovery surface,
            // not a mapping continuation. Keep the still-live deadline
            // represented in the host result instead of accidentally
            // stopping its timer.
            result.mappingDeadlineGeneration =
                session.generation;
            result.mappingTimeoutMilliseconds =
                timeoutMilliseconds;
        }
        return result;
    }

    const Mode before = effectiveMode();
    bool atRoot = inputHintSession->atRoot;
    if (input.hasPendingTypeahead()) {
        static_cast<void>(input.retreatPendingPrefix());
        atRoot = !input.hasPendingTypeahead();
        if (atRoot) {
            prefix = CommandPrefix::None;
            normalCount = 0;
            pendingOperator.reset();
            pendingArgument.reset();
        }
    } else if (!atRoot && nativeGrammarPending()) {
        retreatNativeHintPrefix();
        atRoot = !nativeGrammarPending();
    } else {
        atRoot = true;
    }

    const auto foundView = views.find(view);
    const std::optional<BufferId> buffer =
        foundView != views.end()
            && foundView->second.buffer != 0
        ? std::optional<BufferId>(
              foundView->second.buffer)
        : std::nullopt;
    const std::uint64_t generation =
        ++inputGeneration;
    pendingGeneration = generation;
    pendingView = view;
    pendingTarget = inputTarget;

    std::optional<InputHintSnapshot> snapshot;
    if (atRoot) {
        snapshot = input.rootPrefixSnapshot(
            mappingModeFor(effectiveMode()),
            buffer,
            generation,
            view);
    } else if (input.hasPendingTypeahead()) {
        snapshot = input.pendingPrefixSnapshot(
            mappingModeFor(effectiveMode()),
            buffer,
            generation,
            view);
        if (snapshot) {
            mergeNativeFallbackHints(*snapshot);
        }
    } else {
        snapshot = nativeGrammarSnapshot(generation);
    }

    if (!snapshot || snapshot->candidates.empty()) {
        inputHintSession.reset();
        pendingGeneration.reset();
        pendingView = 0;
        pendingTarget = 0;
        pendingHintPolicy.reset();
        emitModeIfChanged(result, before);
        return result;
    }

    pendingHintPolicy = snapshot->waitPolicy;
    inputHintSession->generation = generation;
    inputHintSession->atRoot = atRoot;
    result.disposition = InputDisposition::Pending;
    result.inputHintGeneration = generation;
    result.mappingInputTarget = inputTarget;
    if (pendingHintPolicy
            == InputHintWaitPolicy::TimedMapping) {
        result.mappingDeadlineGeneration = generation;
        result.mappingTimeoutMilliseconds =
            timeoutMilliseconds;
    }
    emitModeIfChanged(result, before);
    return result;
}

[[nodiscard]] DispatchResult VkCore::Implementation::resolveTypeahead(
    const ViewId viewId,
    const InputTargetId inputTarget,
    const bool timedOut)
{
    DispatchResult result;
    result.disposition = InputDisposition::Consumed;
    while (true) {
        const auto foundView = views.find(viewId);
        const std::optional<BufferId> buffer =
            foundView != views.end()
                && foundView->second.buffer != 0
            ? std::optional<BufferId>(
                  foundView->second.buffer)
            : std::nullopt;
        const detail::ResolveStep step =
            input.resolve(
                mappingModeFor(effectiveMode()),
                buffer,
                timedOut,
                viewId);
        switch (step.kind) {
        case detail::ResolveKind::Empty:
            macroPlaybackActive = false;
            macroStepsRemaining = 0;
            if (insertOneNormalPending
                && insertOneNormalSawKey
                && normalCount == 0
                && prefix == CommandPrefix::None
                && !pendingOperator
                && !pendingArgument
                && !pendingCommandLine
                && baseMode != Mode::Visual) {
                finishInsertOneNormal(
                    result, viewId);
            }
            if (nativeGrammarPending()) {
                pendingGeneration =
                    ++inputGeneration;
                pendingView = viewId;
                pendingTarget = inputTarget;
                pendingHintPolicy =
                    InputHintWaitPolicy::
                        PersistentGrammar;
                result.disposition =
                    InputDisposition::Pending;
                result.inputHintGeneration =
                    pendingGeneration;
                result.mappingInputTarget =
                    inputTarget;
                return result;
            }
            pendingGeneration.reset();
            pendingView = 0;
            pendingTarget = 0;
            pendingHintPolicy.reset();
            return result;
        case detail::ResolveKind::NeedMore:
            pendingGeneration = ++inputGeneration;
            pendingView = viewId;
            pendingTarget = inputTarget;
            pendingHintPolicy = step.waitPolicy;
            if (step.waitPolicy
                    == InputHintWaitPolicy::TimedMapping) {
                // Some prefixes are simultaneously user-mapping nodes
                // and native Normal grammar. Neovim's command reader
                // keeps these transactions alive (notably CTRL-W and g)
                // and reads their continuation without mapping only
                // after a trie miss. Timing the mapping node out first
                // made which-key visibly shrink from the mapping/native
                // union to a native-only list and could change behavior
                // while the user was still reading it.
                auto probe = input.pendingPrefixSnapshot(
                    mappingModeFor(effectiveMode()),
                    buffer,
                    *pendingGeneration,
                    viewId);
                if (probe
                    && probe->nativeFallbackReachable) {
                    probe->candidates.clear();
                    mergeNativeFallbackHints(*probe);
                    if (!probe->candidates.empty()) {
                        pendingHintPolicy =
                            InputHintWaitPolicy::
                                PersistentGrammar;
                    }
                }
            }
            result.disposition =
                InputDisposition::Pending;
            result.mappingInputTarget = inputTarget;
            result.inputHintGeneration =
                pendingGeneration;
            if (pendingHintPolicy
                == InputHintWaitPolicy::
                    TimedMapping) {
                result.mappingDeadlineGeneration =
                    pendingGeneration;
                result.mappingTimeoutMilliseconds =
                    timeoutMilliseconds;
            }
            return result;
        case detail::ResolveKind::EmitKey:
        {
            const std::size_t firstEvent =
                result.events.size();
            const bool resolvingMacro =
                macroPlaybackActive;
            macroCommandDisposition =
                MacroCommandDisposition::Continue;
            consumeNormalKey(
                result,
                viewId,
                inputTarget,
                step.key);
            if (resolvingMacro
                && macroCommandDisposition
                    == MacroCommandDisposition::Abort) {
                // Stop the complete (possibly nested/mapped) macro
                // transaction. Physical input cannot interleave this
                // synchronous resolver pass, so no user key is lost.
                input.clear();
                macroPlaybackActive = false;
                macroStepsRemaining = 0;
                macroCommandDisposition =
                    MacroCommandDisposition::Continue;
            }
            if (containsHostBoundaryEvent(
                    result, firstEvent)
                && (!input.empty()
                    || pendingLocationMove.has_value()
                    || pendingDisplayMotion.has_value())) {
                beginHostBarrier(result);
                return result;
            }
            break;
        }
        case detail::ResolveKind::HostAction:
            consumeMappedHostAction(
                result, step.action);
            if (!input.empty()) {
                beginHostBarrier(result);
                return result;
            }
            break;
        case detail::ResolveKind::Command:
            consumeSemanticCommand(
                result,
                step.commandId,
                inputTarget);
            if (!input.empty()) {
                beginHostBarrier(result);
                return result;
            }
            break;
        case detail::ResolveKind::Error: {
            if (input.empty()) {
                macroPlaybackActive = false;
                macroStepsRemaining = 0;
            }
            Event error;
            error.type = EventType::InputError;
            error.message = step.message;
            error.view = viewId;
            result.events.push_back(std::move(error));
            break;
        }
        }
    }
}

} // namespace vkui::vk
