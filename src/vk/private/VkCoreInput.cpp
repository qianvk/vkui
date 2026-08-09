#include "VkCoreInternal.h"
#include "VkCoreNativeGrammar.h"

namespace vkui::vk {

bool VkCore::hasCanonicalInput(
    const QKeyEvent &event) const
{
    return !m_impl->canonicalEvents.keys(event).empty();
}

bool VkCore::shouldCapture(
    ViewId view,
    const QKeyEvent &event) const
{
    if (!m_impl->enabled) {
        return false;
    }
    if (m_impl->pendingHostBarrierGeneration) {
        // Do not let a physical key escape to Qt while an earlier VK
        // transaction is waiting for its host-side state change to commit.
        return true;
    }
    if (!m_impl->views.contains(view)) {
        view = m_impl->activeView;
    }
    if (view == 0) {
        return false;
    }
    const detail::KeySequence &canonical =
        m_impl->canonicalEvents.keys(event);
    if (canonical.empty()) {
        return false;
    }
    const KeyAtom *const key =
        &canonical.front();
    if (m_impl->promptMatches(
            view, m_impl->inputContextTarget)) {
        const bool cancel =
            detail::isSpecialKey(
                *key, SpecialKey::Escape)
            || modifiedCharacter(
                *key, U'c', KeyModifier::Control)
            || modifiedCharacter(
                *key, U'[', KeyModifier::Control);
        // Native text editing, selection, clipboard shortcuts and IME stay
        // with the line editor. VK exclusively owns the two prompt
        // transaction boundaries.
        return cancel
            || detail::isSpecialKey(
                *key, SpecialKey::Enter);
    }
    if (m_impl->inputHintSessionIsLive()) {
        // Once the delayed discovery surface is visible, Core must observe
        // its complete key transaction. Unknown Insert keys are still
        // returned as PassThrough by dispatch() after the presentation lease
        // is ended, so native text input and IME ownership remain intact.
        return true;
    }
    const Mode mode = m_impl->effectiveMode();
    const auto foundView = m_impl->views.find(view);
    const std::optional<BufferId> buffer =
        foundView != m_impl->views.end()
            && foundView->second.buffer != 0
        ? std::optional<BufferId>(
              foundView->second.buffer)
        : std::nullopt;
    const bool pending =
        m_impl->input.hasPendingTypeahead();
    if (pending) {
        // A mapping mismatch still has ordering semantics: the pending
        // literal or completing short mapping must be resolved before Qt
        // receives this physical key. dispatch() preserves an unowned current
        // key as PassThrough after emitting those earlier effects, so native
        // selection and application shortcuts still run in the resulting
        // mode and against the resulting cursor.
        return true;
    }
    if (mode == Mode::Replace) {
        const bool coreReplaceKey =
            detail::isSpecialKey(
                *key, SpecialKey::Escape)
            || modifiedCharacter(
                *key, U'c', KeyModifier::Control)
            || modifiedCharacter(
                *key, U'[', KeyModifier::Control)
            || (key->modifiers == KeyModifier::None
                && isCoreHandledInsertKey(*key));
        if (coreReplaceKey) {
            return true;
        }
        return m_impl->input.hasMappingPrefix(
            MappingMode::Insert,
            buffer,
            *key,
            view);
    }
    if (mode == Mode::Insert) {
        const bool controlCancel =
            modifiedCharacter(
                *key, U'c', KeyModifier::Control)
            || modifiedCharacter(
                *key, U'[', KeyModifier::Control);
        if (detail::isSpecialKey(
                *key, SpecialKey::Escape)
            || controlCancel
            || isInsertKeywordCompletionKey(*key)
            || isInsertEditingCommandKey(*key)
            || isInsertNavigationKey(*key)
            || ((pending || m_impl->nativeGrammarPending())
                && isCoreHandledInsertKey(*key))) {
            return true;
        }
        if (m_impl->recordingMacro
            && !m_impl->macroPlaybackActive) {
            return true;
        }
        return m_impl->input.hasMappingPrefix(
            MappingMode::Insert,
            buffer,
            *key,
            view);
    }

    const bool modified =
        key->modifiers != KeyModifier::None;
    const bool needsOwnershipLookup =
        modified
        && (detail::hasModifier(
                key->modifiers,
                KeyModifier::Super)
            || (foundView != m_impl->views.end()
                && foundView->second.kind
                    == ViewKind::Surface));
    const bool mappingCandidate =
        needsOwnershipLookup
        && (pending
            ? m_impl->input.mappingConsumesAfterAppending(
                  mappingModeFor(mode),
                  buffer,
                  *key,
                  nullptr,
                  view)
            : m_impl->input.hasMappingPrefix(
                  mappingModeFor(mode),
                  buffer,
                  *key,
                  view));
    // Derive ownership from the canonical grammar itself. A handwritten
    // whitelist had drifted from kNormalCommands: Reader surfaces captured
    // Ctrl-D/U but leaked Ctrl-O/I to Qt, making a recorded reference jump
    // impossible to revisit. Unmapped platform shortcuts still pass through.
    const bool grammarCandidate =
        normalCommand(*key).has_value()
        || modifiedCharacter(
            *key, U'w', KeyModifier::Control)
        || modifiedCharacter(
            *key, U'[', KeyModifier::Control);
    if (detail::hasModifier(
            key->modifiers,
            KeyModifier::Super)
        && !mappingCandidate) {
        return false;
    }
    if (foundView != m_impl->views.end()
        && foundView->second.kind == ViewKind::Surface
        && modified
        && !mappingCandidate
        && !grammarCandidate) {
        // A document surface has no Insert-mode text ownership. Unmapped
        // modified keys remain with its native shortcut/action layer (copy,
        // annotations, accessibility, and platform commands). Native Vim
        // viewport/cancel grammar above is explicitly owned by Core.
        return false;
    }
    return true;
}

bool VkCore::pendingMappingConsumes(
    ViewId view,
    const QKeyEvent &event) const
{
    if (!m_impl->enabled) {
        return false;
    }
    if (m_impl->pendingHostBarrierGeneration) {
        return true;
    }
    if (!m_impl->input.hasPendingTypeahead()) {
        return false;
    }
    if (!m_impl->views.contains(view)) {
        view = m_impl->activeView;
    }
    if (view == 0) {
        return false;
    }
    const detail::KeySequence &canonical =
        m_impl->canonicalEvents.keys(event);
    if (canonical.empty()) {
        return false;
    }
    const KeyAtom *const key =
        &canonical.front();
    const auto foundView = m_impl->views.find(view);
    const std::optional<BufferId> buffer =
        foundView != m_impl->views.end()
            && foundView->second.buffer != 0
        ? std::optional<BufferId>(
              foundView->second.buffer)
        : std::nullopt;
    return m_impl->input.mappingConsumesAfterAppending(
        mappingModeFor(m_impl->effectiveMode()),
        buffer,
        *key,
        nullptr,
        view);
}

DispatchResult VkCore::transitionInputContext(
    ViewId view,
    const InputTargetId inputTarget,
    const bool force)
{
    DispatchResult result;
    if (!m_impl->enabled) {
        return result;
    }
    if (view != 0 && !m_impl->views.contains(view)) {
        view = m_impl->activeView;
    }
    if (m_impl->pendingHostBarrierGeneration) {
        // Focus and view changes are a normal part of applying a host action.
        // Remember the visible context, but keep the suspended typeahead
        // untouched until acknowledgeHostBarrier() decides its outcome.
        m_impl->inputContextView = view;
        m_impl->inputContextTarget = inputTarget;
        result.disposition = InputDisposition::Consumed;
        return result;
    }
    if (!force
        && view == m_impl->inputContextView
        && inputTarget == m_impl->inputContextTarget) {
        return result;
    }

    result.disposition = InputDisposition::Consumed;
    const Mode before = m_impl->effectiveMode();
    const bool changedContext =
        view != m_impl->inputContextView
        || inputTarget != m_impl->inputContextTarget;
    const bool leftPromptContext =
        changedContext
        && m_impl->promptMatches(
            m_impl->inputContextView,
            m_impl->inputContextTarget);
    if (leftPromptContext) {
        m_impl->finishPrompt(
            result, EventType::PromptCancelled);
    }
    if (changedContext
        && before == Mode::Replace) {
        const WindowId replaceWindow =
            m_impl->replaceSession
            ? m_impl->replaceSession->window
            : m_impl->activeView;
        m_impl->finishReplaceMode(
            &result, replaceWindow, false);
    }
    if (before == Mode::Insert
        && m_impl->input.hasPendingTypeahead()
        && m_impl->pendingGeneration
        && m_impl->pendingView != 0) {
        // Resolve against the context where the prefix originated. Events
        // retain that target, so queued text can never leak into the newly
        // focused widget.
        result = flushPendingInput();
    } else {
        m_impl->clearPendingState();
        m_impl->emitModeIfChanged(result, before);
    }
    if (changedContext
        && (before == Mode::Insert
            || before == Mode::Replace)) {
        m_impl->closeInsertUndoBlock();
    }
    if (changedContext
        && m_impl->baseMode == Mode::Visual) {
        // Report the mode transition at the focus boundary itself. Waiting
        // for the next physical key would leave the host painting a stale
        // selection and would make that first key appear to be swallowed.
        (void)m_impl->cancelStaleVisual(result, view);
    }

    const auto nextView = m_impl->views.find(view);
    if (nextView != m_impl->views.end()
        && nextView->second.kind != ViewKind::Editor
        && m_impl->baseMode != Mode::Normal) {
        const Mode modeBeforeReset =
            m_impl->effectiveMode();
        if (m_impl->baseMode == Mode::Insert) {
            m_impl->finishInsertRepeat(&result);
        } else if (m_impl->baseMode == Mode::Replace) {
            m_impl->finishReplaceMode(
                &result, m_impl->activeView, false);
        }
        if (m_impl->baseMode == Mode::Visual) {
            m_impl->leaveVisualMode();
        }
        m_impl->baseMode = Mode::Normal;
        m_impl->pendingOperator.reset();
        m_impl->prefix = CommandPrefix::None;
        m_impl->normalCount = 0;
        m_impl->emitModeIfChanged(
            result, modeBeforeReset);
    }
    if (m_impl->baseMode == Mode::Insert
        || m_impl->baseMode == Mode::Replace) {
        m_impl->continueInsertUndoBlock(view);
    }
    m_impl->inputContextView = view;
    m_impl->inputContextTarget = inputTarget;
    return result;
}

DispatchResult VkCore::dispatch(
    ViewId view,
    const InputTargetId inputTarget,
    const QKeyEvent &event)
{
    if (m_impl->pendingHostBarrierGeneration) {
        DispatchResult blocked;
        blocked.disposition = InputDisposition::Consumed;
        Event error;
        error.type = EventType::InputError;
        error.view = m_impl->activeView;
        error.inputTarget = inputTarget;
        error.message =
            "host action acknowledgement pending";
        blocked.events.push_back(std::move(error));
        return blocked;
    }
    if (!m_impl->views.contains(view)) {
        view = m_impl->activeView;
    }
    if (view == 0) {
        return {};
    }
    const detail::KeySequence &canonical =
        m_impl->canonicalEvents.keys(event);
    if (canonical.empty()) {
        // Physical modifier transitions update Qt's keyboard state but do
        // not exist in Vim typeahead. In particular, pressing Control before
        // Ctrl-D must not settle a pending Leader/grammar transaction.
        DispatchResult ignored;
        ignored.disposition = InputDisposition::PassThrough;
        return ignored;
    }

    DispatchResult boundary =
        transitionInputContext(view, inputTarget, false);
    if (m_impl->activeView != view) {
        // The context transition above already drained/cancelled old input.
        // Do not call setActiveView(), which would silently repeat it.
        m_impl->activeView = view;
    }
    if (m_impl->pendingHostBarrierGeneration) {
        // Resolving the old context may itself have reached a host boundary.
        // The physical key that triggered this dispatch must not overtake it.
        boundary.disposition = InputDisposition::Consumed;
        return boundary;
    }
    const auto prependEvents = [](
        DispatchResult &later,
        DispatchResult &earlier) {
        if (!earlier.events.empty()) {
            later.events.insert(
                later.events.begin(),
                std::make_move_iterator(
                    earlier.events.begin()),
                std::make_move_iterator(
                    earlier.events.end()));
        }
    };

    if (m_impl->promptMatches(view, inputTarget)) {
        const KeyAtom &key = canonical.front();
        const bool submit = detail::isSpecialKey(
            key, SpecialKey::Enter);
        const bool cancel = detail::isSpecialKey(
                key, SpecialKey::Escape)
            || modifiedCharacter(
                key, U'c', KeyModifier::Control)
            || modifiedCharacter(
                key, U'[', KeyModifier::Control);
        if (submit || cancel) {
            boundary.disposition =
                InputDisposition::Consumed;
            m_impl->finishPrompt(
                boundary,
                submit
                    ? EventType::PromptSubmitted
                    : EventType::PromptCancelled);
            return boundary;
        }
        boundary.disposition =
            InputDisposition::PassThrough;
        return boundary;
    }

    std::optional<Implementation::InputHintSession>
        visibleHintSession;
    bool visibleNativeContinuation = false;
    if (m_impl->inputHintSessionIsLive()) {
        visibleHintSession =
            m_impl->inputHintSession;
        visibleNativeContinuation =
            !visibleHintSession->atRoot
            && !m_impl->input.hasPendingTypeahead()
            && m_impl->nativeGrammarPending();
    }
    if (auto sessionResult =
            m_impl->consumeInputHintSessionKey(
                view,
                inputTarget,
                canonical.front())) {
        prependEvents(*sessionResult, boundary);
        return *sessionResult;
    }

    // Neovim's CTRL-W command reader interprets its continuation literally.
    // A pure native prefix has no pending mapping-trie node after duplicate
    // Lua mappings are removed; sending its `l` back through root mapping
    // lookup would let a window-local vkFiles `l` steal the command. Other
    // native states (notably Operator-pending and g) intentionally retain
    // their mapping modes and must not be generalized into this branch.
    bool continueNativeGrammarUnmapped =
        m_impl->prefix == CommandPrefix::Window
        && !m_impl->input.hasPendingTypeahead();
    bool pendingMappingConsumesCurrent = false;
    bool waitsAtDirectMappingChild = false;
    std::optional<KeyAtom> literalBeforeCurrent;
    if (visibleHintSession
        && visibleHintSession->atRoot) {
        const auto foundView =
            m_impl->views.find(view);
        const std::optional<BufferId> buffer =
            foundView != m_impl->views.end()
                && foundView->second.buffer != 0
            ? std::optional<BufferId>(
                  foundView->second.buffer)
            : std::nullopt;
        static_cast<void>(
            m_impl->input.mappingConsumesAfterAppending(
                mappingModeFor(m_impl->effectiveMode()),
                buffer,
                canonical.front(),
                nullptr,
                view,
                nullptr,
                &waitsAtDirectMappingChild));
    }
    if (m_impl->input.hasPendingTypeahead()) {
        const auto foundView = m_impl->views.find(view);
        const std::optional<BufferId> buffer =
            foundView != m_impl->views.end()
                && foundView->second.buffer != 0
            ? std::optional<BufferId>(
                  foundView->second.buffer)
            : std::nullopt;
        pendingMappingConsumesCurrent =
            m_impl->input.mappingConsumesAfterAppending(
                mappingModeFor(m_impl->effectiveMode()),
                buffer,
                canonical.front(),
                nullptr,
                view,
                &literalBeforeCurrent,
                &waitsAtDirectMappingChild);
    }
    if (m_impl->input.hasPendingTypeahead()
        && (!pendingMappingConsumesCurrent
            || literalBeforeCurrent.has_value())) {
        DispatchResult drained = flushPendingInput();
        prependEvents(drained, boundary);
        boundary = std::move(drained);
        if (m_impl->pendingHostBarrierGeneration) {
            boundary.disposition =
                InputDisposition::Consumed;
            return boundary;
        }
        // Keep the mapping miss and its native continuation atomic.  The
        // mismatching physical key has already participated in resolving the
        // pending trie; allowing it through mapping lookup a second time can
        // let a window-local single-key mapping steal the tail of commands
        // such as `<C-w>l`.
        continueNativeGrammarUnmapped =
            m_impl->nativeGrammarPending();
    }
    const bool capture = shouldCapture(view, event);
    if (!capture) {
        if (m_impl->effectiveMode() != Mode::Insert) {
            DispatchResult cancelled =
                transitionInputContext(
                    view, inputTarget, true);
            prependEvents(cancelled, boundary);
            boundary = std::move(cancelled);
        }
        boundary.disposition =
            InputDisposition::PassThrough;
        return boundary;
    }

    if (m_impl->recordingMacro
        && !m_impl->macroPlaybackActive) {
        constexpr std::size_t MaximumRecordedKeys =
            100'000;
        for (const KeyAtom &key : canonical) {
            const bool stopRecording =
                m_impl->baseMode == Mode::Normal
                && !m_impl->pendingOperator
                && !m_impl->pendingArgument
                && m_impl->prefix == CommandPrefix::None
                && plainCharacter(key, U'q');
            if (stopRecording) {
                m_impl->recordingMacro.reset();
                boundary.disposition =
                    InputDisposition::Consumed;
                return boundary;
            }
            RegisterValue &recording =
                m_impl->namedRegisters[
                    *m_impl->recordingMacro];
            if (recording.macroKeys.size()
                >= MaximumRecordedKeys) {
                m_impl->recordingMacro.reset();
                Event error;
                error.type = EventType::InputError;
                error.view = view;
                error.message =
                    "macro recording exceeded its key limit";
                boundary.events.push_back(
                    std::move(error));
                boundary.disposition =
                    InputDisposition::Consumed;
                return boundary;
            }
            // Record physical, canonical input before mappings expand it.
            // Replaying therefore follows the current keymap exactly like
            // Neovim instead of serializing mapping implementation details.
            recording.macroKeys.push_back(key);
            appendRecordedKeyText(recording.text, key);
            recording.linewise = false;
            recording.blockwise = false;
        }
    }

    ++m_impl->inputGeneration;
    m_impl->pendingGeneration.reset();
    m_impl->pendingView = 0;
    m_impl->pendingTarget = 0;
    m_impl->pendingHintPolicy.reset();
    for (const KeyAtom &key : canonical) {
        if (continueNativeGrammarUnmapped) {
            m_impl->input.appendUnmapped(key);
        } else {
            m_impl->input.appendTyped(key);
        }
    }

    DispatchResult result = m_impl->resolveTypeahead(
        view, inputTarget, false);
    const bool sameVisibleState =
        waitsAtDirectMappingChild
        || visibleNativeContinuation
        || continueNativeGrammarUnmapped;
    if (visibleHintSession
        && sameVisibleState
        && result.inputHintGeneration
        && m_impl->pendingGeneration
        && *m_impl->pendingGeneration
            == *result.inputHintGeneration) {
        // which-key updates a valid child inside its existing visible state.
        // Transfer only the presentation lease; the resolver generation and
        // candidate data remain wholly Core-owned. A mapping action or an
        // unrelated new trigger never reaches this branch and therefore
        // re-enters through the ordinary presentation delay.
        visibleHintSession->generation =
            *result.inputHintGeneration;
        visibleHintSession->atRoot = false;
        m_impl->inputHintSession =
            std::move(*visibleHintSession);
    }
    prependEvents(result, boundary);
    return result;
}

DispatchResult VkCore::mappingTimedOut(
    const std::uint64_t generation)
{
    if (!m_impl->enabled
        || m_impl->pendingHostBarrierGeneration
        || !m_impl->pendingGeneration
        || *m_impl->pendingGeneration != generation
        || m_impl->pendingHintPolicy
            != InputHintWaitPolicy::TimedMapping
        || m_impl->pendingView == 0) {
        return {};
    }
    const ViewId view = m_impl->pendingView;
    const InputTargetId inputTarget =
        m_impl->pendingTarget;
    m_impl->pendingGeneration.reset();
    m_impl->pendingView = 0;
    m_impl->pendingTarget = 0;
    m_impl->pendingHintPolicy.reset();
    return m_impl->resolveTypeahead(
        view, inputTarget, true);
}

DispatchResult VkCore::flushPendingInput()
{
    if (!m_impl->enabled
        || m_impl->pendingHostBarrierGeneration
        || !m_impl->pendingGeneration
        || m_impl->pendingView == 0) {
        return {};
    }
    const ViewId view = m_impl->pendingView;
    const InputTargetId inputTarget =
        m_impl->pendingTarget;
    if (m_impl->pendingHintPolicy
            == InputHintWaitPolicy::
                PersistentGrammar
        && !m_impl->input.hasPendingTypeahead()) {
        DispatchResult cancelled;
        cancelled.disposition =
            InputDisposition::Consumed;
        const Mode before = m_impl->effectiveMode();
        m_impl->prefix = CommandPrefix::None;
        m_impl->normalCount = 0;
        m_impl->pendingOperator.reset();
        m_impl->pendingArgument.reset();
        m_impl->pendingGeneration.reset();
        m_impl->pendingView = 0;
        m_impl->pendingTarget = 0;
        m_impl->pendingHintPolicy.reset();
        if (m_impl->insertOneNormalPending) {
            m_impl->finishInsertOneNormal(
                cancelled, view);
        }
        m_impl->emitModeIfChanged(
            cancelled, before);
        return cancelled;
    }
    m_impl->pendingGeneration.reset();
    m_impl->pendingView = 0;
    m_impl->pendingTarget = 0;
    m_impl->pendingHintPolicy.reset();
    return m_impl->resolveTypeahead(
        view, inputTarget, true);
}

std::optional<MappingPrefixSnapshot>
VkCore::pendingMappingSnapshot(
    const std::uint64_t generation) const
{
    return pendingInputHint(generation);
}

std::optional<InputHintSnapshot>
VkCore::pendingInputHint(
    const std::uint64_t generation) const
{
    if (!m_impl->enabled
        || m_impl->pendingHostBarrierGeneration
        || !m_impl->pendingGeneration
        || *m_impl->pendingGeneration != generation
        || m_impl->pendingView == 0) {
        return std::nullopt;
    }
    const auto found =
        m_impl->views.find(m_impl->pendingView);
    if (found == m_impl->views.end()) {
        return std::nullopt;
    }
    const std::optional<BufferId> buffer =
        found->second.buffer == 0
        ? std::nullopt
        : std::optional<BufferId>(
              found->second.buffer);
    if (m_impl->inputHintSession
        && m_impl->inputHintSession->atRoot
        && m_impl->inputHintSession->generation
            == generation) {
        return m_impl->input.rootPrefixSnapshot(
            mappingModeFor(m_impl->effectiveMode()),
            buffer,
            generation,
            m_impl->pendingView);
    }
    if (m_impl->input.hasPendingTypeahead()) {
        std::optional<InputHintSnapshot> snapshot =
            m_impl->input.pendingPrefixSnapshot(
            mappingModeFor(m_impl->effectiveMode()),
            buffer,
            generation,
            m_impl->pendingView);
        if (snapshot) {
            snapshot->waitPolicy =
                m_impl->pendingHintPolicy.value_or(
                    snapshot->waitPolicy);
            m_impl->mergeNativeFallbackHints(
                *snapshot);
        }
        return snapshot;
    }
    return m_impl->nativeGrammarSnapshot(
        generation);
}

bool VkCore::beginInputHintSession(
    const std::uint64_t generation,
    std::string pageDownCommand,
    std::string pageUpCommand)
{
    if (!m_impl->enabled
        || pageDownCommand.empty()
        || pageUpCommand.empty()
        || !m_impl->pendingGeneration
        || *m_impl->pendingGeneration != generation
        || !pendingInputHint(generation)) {
        return false;
    }
    const bool atRoot =
        m_impl->inputHintSession
        && m_impl->inputHintSession->generation
            == generation
        && m_impl->inputHintSession->atRoot;
    m_impl->inputHintSession =
        Implementation::InputHintSession{
            generation,
            std::move(pageDownCommand),
            std::move(pageUpCommand),
            atRoot};
    return true;
}

void VkCore::endInputHintSession()
{
    m_impl->endInputHintPresentation();
}

bool VkCore::inputHintSessionActive() const noexcept
{
    return m_impl->inputHintSessionIsLive();
}

DispatchResult VkCore::resolveDisplayMotion(
    const DisplayMotionRequestId request,
    std::optional<DisplayMotionResolution> resolution)
{
    if (!m_impl->enabled
        || !m_impl->pendingDisplayMotion
        || m_impl->pendingDisplayMotion->id != request
        || !m_impl->pendingHostBarrierGeneration) {
        return {};
    }

    const Implementation::PendingDisplayMotion transaction =
        *m_impl->pendingDisplayMotion;
    const std::uint64_t generation =
        *m_impl->pendingHostBarrierGeneration;
    m_impl->pendingDisplayMotion.reset();

    DispatchResult measured;
    measured.disposition = InputDisposition::Consumed;
    bool valid = resolution.has_value();
    const auto foundView = m_impl->views.find(
        transaction.window);
    const auto foundBuffer = m_impl->buffers.find(
        transaction.buffer);
    valid = valid
        && foundView != m_impl->views.end()
        && foundBuffer != m_impl->buffers.end()
        && foundView->second.buffer == transaction.buffer
        && foundBuffer->second.revision()
            == transaction.revision
        && transaction.operatorPending
            == m_impl->pendingOperator.has_value();
    if (valid && transaction.operatorPending) {
        valid = m_impl->pendingOperator->buffer
                == transaction.buffer
            && m_impl->pendingOperator->start
                == transaction.origin;
    }

    if (valid) {
        const Implementation::Buffer &buffer =
            foundBuffer->second;
        if (verticalDisplayMotion(transaction.command)
            && !resolution->rowGoalColumn) {
            valid = false;
        }
        const bool block = m_impl->ownsVisualBlock(
            transaction.window, transaction.buffer);
        const Cursor destination =
            resolution->destination.buffer;
        valid = valid
            && destination.line
                < buffer.lineStarts.size()
            && m_impl->clampCursor(
                   buffer, destination, block)
                == destination;
        if (valid
            && block
            && m_impl->virtualEdit
                != VirtualEditMode::Block) {
            valid = resolution->destination.column
                <= m_impl->displayLine(
                    buffer, destination.line).width;
        }
        if (valid) {
            const DisplayPosition normalized =
                m_impl->displayPositionForColumn(
                    buffer,
                    destination.line,
                    resolution->destination.column);
            valid = normalized.buffer == destination;
            if (valid && !block) {
                valid = resolution->destination.column
                    == m_impl->displayColumnForBufferColumn(
                        buffer,
                        destination.line,
                        destination.column);
            }
        }
    }

    if (valid) {
        m_impl->applyResolvedDisplayMotion(
            measured, transaction, *resolution);
    } else if (resolution) {
        Event error;
        error.type = EventType::InputError;
        error.view = transaction.window;
        error.buffer = transaction.buffer;
        error.message =
            "stale or invalid display motion resolution";
        measured.events.push_back(std::move(error));
    }

    DispatchResult resumed = acknowledgeHostBarrier(
        generation,
        valid,
        transaction.window,
        transaction.inputTarget);
    if (!measured.events.empty()) {
        resumed.events.insert(
            resumed.events.begin(),
            std::make_move_iterator(
                measured.events.begin()),
            std::make_move_iterator(
                measured.events.end()));
    }
    if (resumed.disposition == InputDisposition::PassThrough) {
        resumed.disposition = InputDisposition::Consumed;
    }
    return resumed;
}

DispatchResult VkCore::acknowledgeHostBarrier(
    const std::uint64_t generation,
    const bool committed,
    ViewId resultingView,
    const InputTargetId resultingTarget)
{
    if (!m_impl->enabled
        || !m_impl->pendingHostBarrierGeneration
        || *m_impl->pendingHostBarrierGeneration
            != generation) {
        return {};
    }

    if (m_impl->pendingDisplayMotion) {
        // A display-row request has a typed destination payload and cannot be
        // committed by the generic boolean host acknowledgement. Rejecting
        // here prevents an older host from clearing the barrier while leaving
        // an orphaned renderer transaction behind.
        DispatchResult rejected;
        rejected.disposition = InputDisposition::Consumed;
        Event error;
        error.type = EventType::InputError;
        error.view = m_impl->pendingDisplayMotion->window;
        error.buffer = m_impl->pendingDisplayMotion->buffer;
        error.message =
            "display motion requires resolveDisplayMotion()";
        rejected.events.push_back(std::move(error));
        m_impl->clearPendingState();
        m_impl->inputContextView = 0;
        m_impl->inputContextTarget = 0;
        return rejected;
    }

    DispatchResult boundary;
    boundary.disposition = InputDisposition::Consumed;
    m_impl->pendingHostBarrierGeneration.reset();

    if (!committed) {
        // A rejected save, activation, or focus transaction invalidates every
        // key generated by the same mapping RHS.
        m_impl->clearPendingState();
        m_impl->inputContextView = 0;
        m_impl->inputContextTarget = 0;
        return boundary;
    }

    if (!m_impl->views.contains(resultingView)) {
        resultingView = m_impl->activeView;
    }
    if (resultingView == 0
        || !m_impl->views.contains(resultingView)) {
        m_impl->clearPendingState();
        Event error;
        error.type = EventType::InputError;
        error.message =
            "host action committed without a valid resulting view";
        boundary.events.push_back(std::move(error));
        return boundary;
    }

    if (m_impl->pendingLocationMove) {
        const Implementation::PendingLocationMove transaction =
            *m_impl->pendingLocationMove;
        m_impl->pendingLocationMove.reset();
        const auto movedView = m_impl->views.find(
            transaction.window);
        const auto movedBuffer = m_impl->buffers.find(
            transaction.destination.buffer);
        if (movedView == m_impl->views.end()
            || movedBuffer == m_impl->buffers.end()
            || movedView->second.buffer
                != transaction.destination.buffer) {
            // The host acknowledged an activation without binding the
            // requested authoritative buffer. Treat that as a failed
            // transaction: no jump-list state may leak from it.
            m_impl->clearPendingState();
            Event error;
            error.type = EventType::InputError;
            error.view = transaction.window;
            error.buffer = transaction.destination.buffer;
            error.message =
                "buffer activation committed without requested buffer";
            boundary.events.push_back(std::move(error));
            return boundary;
        }
        Implementation::View &view = movedView->second;
        const Cursor target = m_impl->cursorAtOffset(
            movedBuffer->second,
            transaction.destination.offset);
        view.cursors[view.buffer] = target;
        view.preferredColumn.reset();
        if (transaction.record) {
            m_impl->recordJump(
                view,
                transaction.origin,
                transaction.destination);
        }
        if (transaction.jumpListAfterCommit) {
            view.jumpList =
                *transaction.jumpListAfterCommit;
        }
        if (transaction.jumpIndexAfterCommit) {
            view.jumpIndex =
                *transaction.jumpIndexAfterCommit;
        }
        m_impl->emitCursor(
            boundary, transaction.window, view);
    }

    if (m_impl->activeView != resultingView) {
        m_impl->closeInsertUndoBlock();
    }
    m_impl->activeView = resultingView;
    m_impl->inputContextView = resultingView;
    m_impl->inputContextTarget = resultingTarget;

    const auto resulting =
        m_impl->views.find(resultingView);
    if (resulting->second.kind != ViewKind::Editor
        && m_impl->baseMode != Mode::Normal) {
        const Mode before = m_impl->effectiveMode();
        if (m_impl->baseMode == Mode::Insert) {
            m_impl->finishInsertRepeat(&boundary);
        } else if (m_impl->baseMode == Mode::Replace) {
            m_impl->finishReplaceMode(
                &boundary,
                m_impl->activeView,
                false);
        }
        if (m_impl->baseMode == Mode::Visual) {
            m_impl->leaveVisualMode();
        }
        m_impl->baseMode = Mode::Normal;
        m_impl->pendingOperator.reset();
        m_impl->prefix = CommandPrefix::None;
        m_impl->normalCount = 0;
        m_impl->emitModeIfChanged(boundary, before);
    }
    if (m_impl->baseMode == Mode::Insert
        || m_impl->baseMode == Mode::Replace) {
        m_impl->continueInsertUndoBlock(
            resultingView);
    }

    if (m_impl->input.empty()) {
        return boundary;
    }

    DispatchResult resumed = m_impl->resolveTypeahead(
        resultingView, resultingTarget, false);
    if (!boundary.events.empty()) {
        resumed.events.insert(
            resumed.events.begin(),
            std::make_move_iterator(
                boundary.events.begin()),
            std::make_move_iterator(
                boundary.events.end()));
    }
    return resumed;
}

void VkCore::setLeader(std::u32string notation)
{
    const detail::ParseResult parsed =
        m_impl->input.parse(notation);
    if (parsed && !parsed.keys.empty()) {
        m_impl->input.setLeader(parsed.keys);
    }
}

void VkCore::setLocalLeader(std::u32string notation)
{
    const detail::ParseResult parsed =
        m_impl->input.parse(notation);
    if (parsed && !parsed.keys.empty()) {
        m_impl->input.setLocalLeader(parsed.keys);
    }
}

std::vector<MappingId> VkCore::addMappings(
    const std::span<const MappingDefinition> definitions,
    std::string *const error)
{
    for (const MappingDefinition &definition : definitions) {
        if (definition.window
            && !m_impl->views.contains(
                *definition.window)) {
            if (error != nullptr) {
                *error =
                    "window-local mapping requires a live window";
            }
            return {};
        }
        if (definition.buffer
            && !m_impl->buffers.contains(
                *definition.buffer)) {
            if (error != nullptr) {
                *error =
                    "buffer-local mapping requires a live buffer";
            }
            return {};
        }
    }
    return m_impl->input.addMappings(
        definitions, error);
}

MappingId VkCore::addMapping(
    const MappingDefinition &definition,
    std::string *const error)
{
    const std::span definitions(&definition, 1);
    std::vector<MappingId> ids = addMappings(
        definitions, error);
    return ids.empty() ? 0 : ids.front();
}

bool VkCore::removeMapping(const MappingId id)
{
    const std::array ids{id};
    return removeMappings(ids).removed == 1;
}

MappingRevocation VkCore::removeMappings(
    const std::span<const MappingId> ids)
{
    MappingRevocation result;
    if (ids.empty()) {
        return result;
    }

    bool invalidatesPending = false;
    if (m_impl->pendingGeneration
        && !m_impl->pendingHostBarrierGeneration
        && m_impl->pendingView != 0) {
        const auto foundView = m_impl->views.find(
            m_impl->pendingView);
        if (foundView != m_impl->views.end()) {
            const std::optional<BufferId> buffer =
                foundView->second.buffer == 0
                ? std::nullopt
                : std::optional<BufferId>(
                      foundView->second.buffer);
            invalidatesPending =
                m_impl->input
                    .pendingResolutionUsesAnyMapping(
                        ids,
                        mappingModeFor(
                            m_impl->effectiveMode()),
                        buffer,
                        m_impl->pendingView);
        }
    }

    result.removed = m_impl->input.removeMappings(ids);
    if (result.removed != 0 && invalidatesPending) {
        result.invalidatedInputGeneration =
            m_impl->pendingGeneration;
        cancelPendingInput();
    }
    return result;
}

void VkCore::setMappingTimeoutMilliseconds(
    const int value)
{
    m_impl->timeoutMilliseconds =
        std::clamp(value, 0, 60'000);
}

int VkCore::mappingTimeoutMilliseconds() const noexcept
{
    return m_impl->timeoutMilliseconds;
}

VkUserCommandRegistry &VkCore::userCommands() noexcept
{
    return m_impl->userCommands;
}

const VkUserCommandRegistry &
VkCore::userCommands() const noexcept
{
    return m_impl->userCommands;
}

void VkCore::cancelPendingInput()
{
    m_impl->clearPendingState();
    m_impl->inputContextView = 0;
    m_impl->inputContextTarget = 0;
}

} // namespace vkui::vk
