#include <vkui/vk/VkCanonicalKeyEvent.h>
#include <vkui/vk/VkCore.h>
#include <vkui/vk/VkWindowCommands.h>

#include <QElapsedTimer>
#include <QKeyEvent>
#include <QTest>

#include <algorithm>
#include <array>
#include <optional>

using namespace vkui::vk;

namespace {

[[nodiscard]] Qt::KeyboardModifier vimControlModifier()
{
#ifdef Q_OS_MACOS
    return Qt::MetaModifier;
#else
    return Qt::ControlModifier;
#endif
}

[[nodiscard]] DispatchResult press(
    VkCore &core,
    const ViewId view,
    const int key,
    const Qt::KeyboardModifiers modifiers = Qt::NoModifier,
    const QString &text = {})
{
    const QKeyEvent event(
        QEvent::KeyPress,
        key,
        modifiers,
        text);
    return core.dispatch(view, view, event);
}

[[nodiscard]] DispatchResult pressAscii(
    VkCore &core,
    const ViewId view,
    const QChar value)
{
    const int key = value.isLetter()
        ? Qt::Key_A
            + value.toUpper().unicode() - QChar(u'A').unicode()
        : value.isDigit()
        ? Qt::Key_0
            + value.unicode() - QChar(u'0').unicode()
        : value == QChar(u' ')
        ? Qt::Key_Space
        : value == QChar(u'[')
        ? Qt::Key_BracketLeft
        : value == QChar(u']')
        ? Qt::Key_BracketRight
        : value == QChar(u',')
        ? Qt::Key_Comma
        : 0;
    return press(
        core,
        view,
        key,
        value.isUpper()
            ? Qt::ShiftModifier
            : Qt::NoModifier,
        QString(value));
}

[[nodiscard]] bool hasHostAction(
    const DispatchResult &result,
    const HostAction action)
{
    return std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [action](const Event &event) {
            return event.type == EventType::HostAction
                && event.hostAction == action;
        });
}

[[nodiscard]] bool hasCommandRequest(
    const DispatchResult &result,
    const std::string_view command)
{
    return std::ranges::any_of(
        result.events,
        [command](const Event &event) {
            return event.type
                    == EventType::CommandRequested
                && event.commandId == command;
        });
}

[[nodiscard]] std::optional<Cursor> lastCursor(
    const DispatchResult &result)
{
    for (auto event = result.events.crbegin();
         event != result.events.crend();
         ++event) {
        if (event->type == EventType::CursorChanged) {
            return event->cursor;
        }
    }
    return std::nullopt;
}

[[nodiscard]] const Event *displayMotionRequest(
    const DispatchResult &result)
{
    const auto found = std::ranges::find_if(
        result.events,
        [](const Event &event) {
            return event.type
                == EventType::DisplayMotionRequested;
        });
    return found == result.events.cend()
        ? nullptr
        : &*found;
}

[[nodiscard]] std::optional<BufferId>
lastActivationRequest(const DispatchResult &result)
{
    for (auto event = result.events.crbegin();
         event != result.events.crend();
         ++event) {
        if (event->type
            == EventType::BufferActivationRequested) {
            return event->buffer;
        }
    }
    return std::nullopt;
}

[[nodiscard]] MappingDefinition actionMapping(
    std::u32string lhs,
    const HostAction action,
    const MappingModes modes =
        mappingModes(MappingMode::Normal),
    const bool nowait = false,
    const std::optional<BufferId> buffer = std::nullopt)
{
    MappingDefinition definition;
    definition.modes = modes;
    definition.lhs = std::move(lhs);
    definition.target = HostActionTarget{action};
    definition.options = MappingOptions{
        RemapPolicy::NoRemap,
        nowait,
        true};
    definition.buffer = buffer;
    return definition;
}

[[nodiscard]] MappingDefinition keyMapping(
    std::u32string lhs,
    std::u32string rhs,
    const RemapPolicy remap = RemapPolicy::Recursive,
    const MappingModes modes =
        mappingModes(MappingMode::Normal))
{
    MappingDefinition definition;
    definition.modes = modes;
    definition.lhs = std::move(lhs);
    definition.target =
        KeySequenceTarget{std::move(rhs)};
    definition.options =
        MappingOptions{remap, false, false};
    return definition;
}

[[nodiscard]] std::u16string insertedText(
    const DispatchResult &result)
{
    std::u16string text;
    for (const Event &event : result.events) {
        if (event.type == EventType::InsertText) {
            text += event.editInserted;
        }
    }
    return text;
}

[[nodiscard]] bool hasInsertCommand(
    const DispatchResult &result,
    const InsertCommand command)
{
    return std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [command](const Event &event) {
            return event.type == EventType::InsertCommand
                && event.insertCommand == command;
        });
}

class CorePagedProvider final
    : public vkui::buffer::IRangeProvider
{
public:
    [[nodiscard]] vkui::buffer::Descriptor
    describe() const override
    {
        return vkui::buffer::Descriptor{
            vkui::buffer::Identity{"core-paged-source"},
            31,
            sourceSize,
            false};
    }

    [[nodiscard]] vkui::buffer::RangeRead read(
        const vkui::buffer::RangeRequest &request)
        const override
    {
        largestRead = std::max(
            largestRead, request.maximumLength);
        if (request.offset < residentOffset
            || request.offset + request.maximumLength
                > residentOffset + residentLength) {
            return vkui::buffer::RangeRead{
                vkui::buffer::ReadStatus::Pending,
                vkui::buffer::Identity{
                    "core-paged-source"},
                31,
                request.offset,
                sourceSize,
                {}};
        }
        return vkui::buffer::RangeRead{
            vkui::buffer::ReadStatus::Ok,
            vkui::buffer::Identity{"core-paged-source"},
            31,
            request.offset,
            sourceSize,
            std::u16string(request.maximumLength, u'p')};
    }

    [[nodiscard]] bool requestPrefetch(
        const vkui::buffer::PrefetchRequest &request,
        const std::stop_token stopToken,
        vkui::buffer::PrefetchCompletion completion)
        const override
    {
        if (stopToken.stop_requested()) {
            return false;
        }
        residentOffset = request.offset;
        residentLength = request.maximumLength;
        largestPrefetch = std::max(
            largestPrefetch, request.maximumLength);
        completion(vkui::buffer::PrefetchResult{
            vkui::buffer::PrefetchStatus::Ready,
            request.expectedRevision,
            request.offset,
            request.maximumLength,
            request.generation});
        return true;
    }

    static constexpr std::size_t sourceSize =
        128U * 1024U * 1024U;
    mutable std::size_t residentOffset = sourceSize;
    mutable std::size_t residentLength = 0;
    mutable std::size_t largestRead = 0;
    mutable std::size_t largestPrefetch = 0;
};

} // namespace

class VkCoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void canonicalEventCacheSharesQtDeliveryPhases();
    void disabledCoreHasNoModeAndPassesEveryKey();
    void leaderExpandsWhenMappingIsDefined();
    void modifierOnlyEventPreservesPendingTypeahead();
    void visibleInputHintSessionOwnsOnlyPresentationControls();
    void inputHintsCoverPersistentLeaderAndNativeGrammar();
    void exactLeaderMappingResolvesAfterTimeout();
    void leaderContinuationWinsBeforeExactTimeout();
    void nativeGrammarDescriptorsKeepHintsAndExecutionAligned();
    void nativeWindowGrammarSharesDiscoveryExecutionAndAliases();
    void longestMatchWaitsAndNowaitDoesNot();
    void recursiveAndNoremapRhsUseTypeaheadFlags();
    void failedPrefixPreservesEveryTypedKey();
    void recursiveCycleIsBounded();
    void recursiveMappingWithLiteralProgressCannotBlockHost();
    void operatorPendingIsARealMappingMode();
    void hostActionMappingsTerminateNormalGrammar();
    void bufferLocalMappingPrecedesGlobalMapping();
    void windowLocalMappingPrecedesBufferAndGlobal();
    void windowLocalMappingShadowsNativeCandidateAndPreservesFallback();
    void exactShortMappingSuppressesUnreachableNativeFallback();
    void localShortMappingWaitsForLongerGlobalUnlessNowait();
    void mappingRedefinitionDoesNotResurrectOverlappingModes();
    void mappingsCannotReplaceAnotherPluginOwner();
    void mappingBatchValidatesEveryLiveScopeWithoutConsumingIds();
    void insertTextPassesThroughAndEscapeReturnsToNormal();
    void insertMappingsReplayTextWithoutLosingPrefixes();
    void insertMappingsRejectUnsupportedAtomicEffects();
    void pendingInsertPrefixDoesNotSwallowNativeModifiedKey();
    void recursiveInsertProbeUsesTheRealResolver();
    void nativeShortcutSettlesPrefixBeforeModeDecision();
    void nativeWindowContinuationCannotBeStolenByLocalMapping();
    void inputContextBoundariesDrainOrCancelPendingState();
    void navigationViewsCannotEnterOperatorPending();
    void editorWithoutBufferCannotEnterModalState();
    void surfaceViewsEmitNavigationAndPreserveNativeShortcuts();
    void readOnlyBuffersRejectNormalEditsInCore();
    void externalEditsUseUtf16AndRemainStrict();
    void bufferHistoryIsCoreOwnedAndExposesCleanState();
    void largeExternalEditsAvoidWholeBufferRescans();
    void displayLayoutCacheStaysSparseForHugeBuffers();
    void staleMappingTimeoutCannotReplayInput();
    void mappingOwnerRevocationCancelsOnlyAffectedPendingPrefix();
    void unicodeMappingUsesLogicalText();
    void countsAndNormalCommandPrefixesAreNotMappings();
    void normalUndoRedoSupportsCountsAndBranches();
    void insertSessionIsOneUndoBlock();
    void insertNavigationStartsANewUndoBlock();
    void controlNAndPMatchNativeLineMotions();
    void nativeColumnPercentageAndControlAliasesMatchNeovim();
    void displayPositionQueryUsesCoreSparseLayout();
    void displayRowMotionsUseARevisionCheckedRendererTransaction();
    void halfPageScrollIsWindowLocal();
    void semanticViewJumpParticipatesInJumpHistory();
    void characterSearchMotionsRepeatAndDriveOperators();
    void matchingPairsAreNestedAndUtf16Safe();
    void windowMotionsUseTheWindowViewport();
    void normalCursorNeverSplitsSurrogatePairs();
    void buffersAndViewsKeepIndependentCursors();
    void bufferActivationRequiresHostCommit();
    void hostBarrierRejectsOrCommitsRemainingBufferRhs();
    void hostBarriersSequenceAcrossBufferActivations();
    void hostBarrierResumesInResultingNavigationContext();
    void dispatchCannotOvertakeBarrierCreatedByPrefixDrain();
    void bufferPathsRebindAndRemoveWithoutGhostEntries();
    void bufferDestructionRevokesOnlyItsLocalInputContributions();
    void boundedBufferReadsCarryRevisionAndPagingState();
    void providerBuffersExposeBoundedDataWithoutUnsafeAttachment();
};

void VkCoreTests::
    canonicalEventCacheSharesQtDeliveryPhases()
{
    detail::CanonicalKeyEventCache cache;
    const QKeyEvent shortcutOverride(
        QEvent::ShortcutOverride,
        Qt::Key_X,
        Qt::ShiftModifier,
        11,
        22,
        33,
        QStringLiteral("X"),
        false,
        1);
    const QKeyEvent keyPress(
        QEvent::KeyPress,
        Qt::Key_X,
        Qt::ShiftModifier,
        11,
        22,
        33,
        QStringLiteral("X"),
        false,
        1);

    const detail::KeySequence first =
        cache.keys(shortcutOverride);
    QCOMPARE(cache.missCount(), std::size_t{1});
    QCOMPARE(cache.hitCount(), std::size_t{0});
    QCOMPARE(first.size(), std::size_t{1});
    QCOMPARE(
        detail::character(first.front()),
        std::optional<char32_t>(U'X'));

    // Event type is deliberately not part of the payload signature: Qt's
    // ShortcutOverride and KeyPress phases for one physical key share the
    // exact canonical atom storage.
    const detail::KeySequence &second =
        cache.keys(keyPress);
    QVERIFY(second == first);
    QCOMPARE(cache.missCount(), std::size_t{1});
    QCOMPARE(cache.hitCount(), std::size_t{1});

    // Native identity and repeat metadata are part of the signature even when
    // the resulting logical character is the same.
    const QKeyEvent otherNativeKey(
        QEvent::KeyPress,
        Qt::Key_X,
        Qt::ShiftModifier,
        11,
        23,
        33,
        QStringLiteral("X"),
        false,
        1);
    QVERIFY(cache.keys(otherNativeKey) == first);
    QCOMPARE(cache.missCount(), std::size_t{2});

    const QKeyEvent autoRepeat(
        QEvent::KeyPress,
        Qt::Key_X,
        Qt::ShiftModifier,
        11,
        23,
        33,
        QStringLiteral("X"),
        true,
        2);
    QVERIFY(cache.keys(autoRepeat) == first);
    QCOMPARE(cache.missCount(), std::size_t{3});
}

void VkCoreTests::disabledCoreHasNoModeAndPassesEveryKey()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    core.setEnabled(false);
    QVERIFY(!core.isEnabled());
    QVERIFY(!core.mode().has_value());
    QCOMPARE(
        pressAscii(core, editor, QChar(u'j')).disposition,
        InputDisposition::PassThrough);

    // With modal input disabled, the host cursor is an insertion boundary,
    // so end-of-line must remain representable instead of being clamped onto
    // the last Normal-mode character.
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "disabled-caret.txt",
        std::u16string(u"alpha"),
        Cursor{0, 5});
    QVERIFY(buffer != 0);
    QCOMPARE(core.window(editor)->cursor, (Cursor{0, 5}));
    QVERIFY(core.applyExternalEdit(
        editor,
        5,
        0,
        std::u16string(u"!"),
        Cursor{0, 6}));
    QCOMPARE(core.window(editor)->cursor, (Cursor{0, 6}));
}

void VkCoreTests::leaderExpandsWhenMappingIsDefined()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    std::string error;
    const MappingId explorer = core.addMapping(
        actionMapping(
            U"<Leader>e",
            HostAction::Cancel),
        &error);
    QVERIFY2(explorer != 0, error.c_str());

    auto result =
        pressAscii(core, editor, QChar(u' '));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    QVERIFY(!result.mappingDeadlineGeneration.has_value());
    QVERIFY(result.inputHintGeneration.has_value());
    result = pressAscii(core, editor, QChar(u'e'));
    QVERIFY(hasHostAction(
        result, HostAction::Cancel));

    core.setLeader(U",");
    const MappingId custom = core.addMapping(
        actionMapping(
            U"<Leader>x",
            HostAction::FocusPanelRight),
        &error);
    QVERIFY2(custom != 0, error.c_str());

    // Existing mappings keep the leader value captured at definition time.
    result = pressAscii(core, editor, QChar(u' '));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, editor, QChar(u'e'));
    QVERIFY(hasHostAction(
        result, HostAction::Cancel));

    result = pressAscii(core, editor, QChar(u','));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, editor, QChar(u'x'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));
}

void VkCoreTests::modifierOnlyEventPreservesPendingTypeahead()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    std::string error;
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"<Leader>e",
                HostAction::Cancel),
            &error)
            != 0,
        error.c_str());

    const DispatchResult leader =
        pressAscii(core, editor, QChar(u' '));
    QVERIFY(leader.inputHintGeneration);
    const std::uint64_t generation =
        *leader.inputHintGeneration;

    const QKeyEvent modifier(
        QEvent::KeyPress,
        Qt::Key_Control,
        Qt::ControlModifier);
    QVERIFY(!core.hasCanonicalInput(modifier));
    const DispatchResult ignored =
        core.dispatch(editor, 0, modifier);
    QCOMPARE(
        ignored.disposition,
        InputDisposition::PassThrough);
    QVERIFY(ignored.events.empty());
    QVERIFY(core.pendingInputHint(generation));

    const DispatchResult completed =
        pressAscii(core, editor, QChar(u'e'));
    QVERIFY(hasHostAction(
        completed, HostAction::Cancel));
}

void VkCoreTests::
    visibleInputHintSessionOwnsOnlyPresentationControls()
{
    constexpr std::string_view pageDown =
        "test.which-key.page-down";
    constexpr std::string_view pageUp =
        "test.which-key.page-up";

    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"zero\none\ntwo",
        {0, 0}) != 0);

    MappingDefinition nested;
    nested.lhs = U"<Leader>fx";
    nested.target = CommandTarget{"test.nested"};
    nested.options.remap = RemapPolicy::NoRemap;
    nested.metadata.description = "Nested command";
    MappingDefinition sibling = nested;
    sibling.lhs = U"<Leader>g";
    sibling.target = CommandTarget{"test.sibling"};
    std::string error;
    const std::array mappings{nested, sibling};
    QCOMPARE(
        core.addMappings(mappings, &error).size(),
        mappings.size());
    QVERIFY2(error.empty(), error.c_str());

    // Before the delayed surface is visible, its presentation controls do
    // not exist. Ctrl-D follows the ordinary resolver/Normal grammar and can
    // never manufacture a which-key plugin command.
    DispatchResult result =
        pressAscii(core, editor, QChar(u' '));
    QVERIFY(result.inputHintGeneration.has_value());
    QVERIFY(!core.inputHintSessionActive());
    result = press(
        core,
        editor,
        Qt::Key_D,
        vimControlModifier(),
        QStringLiteral("d"));
    QVERIFY(!hasCommandRequest(result, pageDown));
    QVERIFY(!hasCommandRequest(result, pageUp));

    core.cancelPendingInput();
    QVERIFY(core.setViewCursor(editor, {0, 0}));

    // Once the UI explicitly leases the live generation, paging emits a
    // semantic child-plugin command without advancing or resolving its real
    // typeahead transaction.
    result = pressAscii(core, editor, QChar(u' '));
    QVERIFY(result.inputHintGeneration.has_value());
    const std::uint64_t leaderGeneration =
        *result.inputHintGeneration;
    QVERIFY(core.beginInputHintSession(
        leaderGeneration,
        std::string(pageDown),
        std::string(pageUp)));
    QVERIFY(core.inputHintSessionActive());
    result = press(
        core,
        editor,
        Qt::Key_D,
        vimControlModifier(),
        QStringLiteral("d"));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    QVERIFY(hasCommandRequest(result, pageDown));
    QCOMPARE(
        result.inputHintGeneration,
        std::optional<std::uint64_t>(leaderGeneration));
    QVERIFY(core.pendingInputHint(leaderGeneration));

    // Escape is an atomic session cancellation. In particular, it must not
    // flush the pending literal Leader (Space) and move the cursor first.
    result = press(
        core,
        editor,
        Qt::Key_Escape);
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QVERIFY(!core.inputHintSessionActive());
    QVERIFY(!core.pendingInputHint(leaderGeneration));
    const auto unchanged = core.window(editor);
    QVERIFY(unchanged.has_value());
    QCOMPARE(unchanged->cursor, Cursor({0, 0}));

    // A key outside the visible Leader node can complete that state and
    // begin an unrelated native trigger in the same physical dispatch. The
    // new hint remains valid, but it must not inherit the visible session;
    // the host will hide the old surface and apply discovery delay again.
    result = pressAscii(core, editor, QChar(u' '));
    QVERIFY(result.inputHintGeneration.has_value());
    QVERIFY(core.beginInputHintSession(
        *result.inputHintGeneration,
        std::string(pageDown),
        std::string(pageUp)));
    result = press(
        core,
        editor,
        Qt::Key_W,
        vimControlModifier(),
        QStringLiteral("w"));
    QVERIFY(result.inputHintGeneration.has_value());
    QVERIFY(!core.inputHintSessionActive());
    const auto independentHint = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(independentHint.has_value());
    QCOMPARE(
        independentHint->prefixNotation,
        std::u32string(U"<C-w>"));
    core.cancelPendingInput();

    // Backspace walks to the parent resolver node and ultimately exposes a
    // detached root snapshot. Neither operation replays a key into Normal
    // mode; closing the detached root leaves no phantom pending input.
    result = pressAscii(core, editor, QChar(u' '));
    QVERIFY(result.inputHintGeneration.has_value());
    QVERIFY(core.beginInputHintSession(
        *result.inputHintGeneration,
        std::string(pageDown),
        std::string(pageUp)));
    result = pressAscii(core, editor, QChar(u'f'));
    QVERIFY(result.inputHintGeneration.has_value());
    const std::uint64_t nestedGeneration =
        *result.inputHintGeneration;
    const auto nestedHint =
        core.pendingInputHint(nestedGeneration);
    QVERIFY(nestedHint.has_value());
    QVERIFY(nestedHint->prefixNotation.ends_with(U"f"));
    // A valid child is still the same upstream state: its visible lease is
    // transferred to the resolver's new generation without another delay.
    QVERIFY(core.inputHintSessionActive());

    result = press(
        core,
        editor,
        Qt::Key_Backspace);
    QVERIFY(result.inputHintGeneration.has_value());
    const auto parentHint = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(parentHint.has_value());
    QCOMPARE(parentHint->prefixNotation, std::u32string(U"<Space>"));
    QVERIFY(core.inputHintSessionActive());

    result = press(
        core,
        editor,
        Qt::Key_Backspace);
    QVERIFY(result.inputHintGeneration.has_value());
    const auto rootHint = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(rootHint.has_value());
    QVERIFY(rootHint->prefixNotation.empty());
    QVERIFY(!rootHint->candidates.empty());
    core.endInputHintSession();
    QVERIFY(!core.inputHintSessionActive());
    QVERIFY(!core.pendingInputHint(
        *result.inputHintGeneration));
}

void VkCoreTests::
    inputHintsCoverPersistentLeaderAndNativeGrammar()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"zero\none",
        {1, 0}) != 0);

    MappingDefinition explorer = actionMapping(
        U"<Leader>e",
        HostAction::Cancel);
    explorer.metadata = MappingMetadata{
        "Toggle file tree",
        "Explorer",
        "vkery.vktree",
        false,
        10};
    std::string error;
    QVERIFY2(
        core.addMapping(explorer, &error) != 0,
        error.c_str());

    auto result =
        pressAscii(core, editor, QChar(u' '));
    QCOMPARE(
        result.disposition,
        InputDisposition::Pending);
    QVERIFY(result.inputHintGeneration.has_value());
    QVERIFY(!result.mappingDeadlineGeneration.has_value());
    const std::uint64_t leaderGeneration =
        *result.inputHintGeneration;
    const auto leader =
        core.pendingInputHint(leaderGeneration);
    QVERIFY(leader.has_value());
    QCOMPARE(
        leader->waitPolicy,
        InputHintWaitPolicy::PersistentLeader);
    QVERIFY(leader->window == editor);
    QVERIFY(std::ranges::any_of(
        leader->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"e"
                && candidate.origin
                    == InputHintOrigin::Mapping
                && candidate.commandId.empty();
        }));

    // timeoutlen is deliberately irrelevant once Leader starts a session.
    const DispatchResult ignored =
        core.mappingTimedOut(leaderGeneration);
    QVERIFY(ignored.events.empty());
    QVERIFY(
        core.pendingInputHint(leaderGeneration)
            .has_value());
    result = pressAscii(core, editor, QChar(u'e'));
    QVERIFY(hasHostAction(
        result, HostAction::Cancel));

    result = pressAscii(core, editor, QChar(u'g'));
    QCOMPARE(
        result.disposition,
        InputDisposition::Pending);
    QVERIFY(result.inputHintGeneration.has_value());
    QVERIFY(!result.mappingDeadlineGeneration.has_value());
    const auto go = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(go.has_value());
    QCOMPARE(
        go->waitPolicy,
        InputHintWaitPolicy::PersistentGrammar);
    QVERIFY(go->prefixNotation == U"g");
    QVERIFY(std::ranges::any_of(
        go->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"g"
                && candidate.description
                    == "First line"
                && candidate.origin
                    == InputHintOrigin::
                        NativeGrammar;
        }));
    QVERIFY(std::ranges::any_of(
        go->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"_";
        }));
    result = pressAscii(core, editor, QChar(u'g'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 0}));

    result = pressAscii(core, editor, QChar(u'd'));
    QCOMPARE(
        result.disposition,
        InputDisposition::Pending);
    const auto deletion = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(deletion.has_value());
    QCOMPARE(
        deletion->waitPolicy,
        InputHintWaitPolicy::PersistentGrammar);
    QVERIFY(deletion->prefixNotation == U"d");
    QVERIFY(deletion->candidates.size() > 20);
    result = press(core, editor, Qt::Key_Escape);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
}

void VkCoreTests::exactLeaderMappingResolvesAfterTimeout()
{
    VkCore core;
    const ViewId editor = core.registerView(ViewKind::Editor);
    std::string error;
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"<Leader>",
                HostAction::FocusPanelLeft),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"<Leader>e",
                HostAction::FocusPanelRight),
            &error)
            != 0,
        error.c_str());

    DispatchResult result =
        pressAscii(core, editor, QChar(u' '));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    QVERIFY(result.mappingDeadlineGeneration.has_value());
    QCOMPARE(
        result.mappingDeadlineGeneration,
        result.inputHintGeneration);
    const std::uint64_t generation =
        *result.mappingDeadlineGeneration;
    const auto hint = core.pendingInputHint(generation);
    QVERIFY(hint.has_value());
    QCOMPARE(
        hint->waitPolicy,
        InputHintWaitPolicy::TimedMapping);

    result = core.mappingTimedOut(generation);
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelLeft));
    QVERIFY(!core.pendingInputHint(generation));
}

void VkCoreTests::leaderContinuationWinsBeforeExactTimeout()
{
    VkCore core;
    const ViewId editor = core.registerView(ViewKind::Editor);
    std::string error;
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"<Leader>",
                HostAction::FocusPanelLeft),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"<Leader>e",
                HostAction::FocusPanelRight),
            &error)
            != 0,
        error.c_str());

    DispatchResult result =
        pressAscii(core, editor, QChar(u' '));
    QVERIFY(result.mappingDeadlineGeneration.has_value());
    const std::uint64_t staleGeneration =
        *result.mappingDeadlineGeneration;
    result = pressAscii(core, editor, QChar(u'e'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));
    QVERIFY(!hasHostAction(
        result, HostAction::FocusPanelLeft));

    // The host timer may already be queued.  Its generation must be stale
    // after the longer mapping has consumed the pending Leader.
    result = core.mappingTimedOut(staleGeneration);
    QVERIFY(result.events.empty());
}

void VkCoreTests::
    nativeGrammarDescriptorsKeepHintsAndExecutionAligned()
{
    VkCore core;
    const WindowId editor =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/native-grammar.txt",
        u"one xx\n  two\ntail",
        {2, 0});
    QVERIFY(buffer != 0);

    // `g` execution and hints are projections of kGotoGrammar.
    auto result =
        pressAscii(core, editor, QChar(u'g'));
    QVERIFY(result.inputHintGeneration);
    const auto go = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(go);
    QCOMPARE(go->prefixNotation, std::u32string(U"g"));
    QVERIFY(std::ranges::any_of(
        go->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"g"
                && candidate.description == "First line"
                && candidate.origin
                    == InputHintOrigin::NativeGrammar;
        }));
    result = pressAscii(core, editor, QChar(u'g'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 0}));

    // Operator continuations come from one table. Character-search motions
    // are intermediate nodes, while <CR> is a real terminal motion.
    result = pressAscii(core, editor, QChar(u'd'));
    QVERIFY(result.inputHintGeneration);
    const auto deletion = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(deletion);
    const auto findCandidate =
        [&deletion](const std::u32string_view key) {
            return std::ranges::find_if(
                deletion->candidates,
                [key](
                    const InputHintCandidate
                        &candidate) {
                    return candidate.keyNotation == key;
                });
        };
    const auto enter = findCandidate(U"<CR>");
    QVERIFY(enter != deletion->candidates.cend());
    QVERIFY(enter->completesMapping);
    QVERIFY(!enter->hasChildren);
    const auto find = findCandidate(U"f");
    QVERIFY(find != deletion->candidates.cend());
    QVERIFY(!find->completesMapping);
    QVERIFY(find->hasChildren);

    result = pressAscii(core, editor, QChar(u'f'));
    QVERIFY(result.inputHintGeneration);
    const auto character = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(character);
    QCOMPARE(
        character->prefixNotation,
        std::u32string(U"df"));
    QCOMPARE(
        character->candidates.size(),
        std::size_t{1});
    QCOMPARE(
        character->candidates.front().keyNotation,
        std::u32string(U"<char>"));
    result = pressAscii(core, editor, QChar(u'x'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"x\n  two\ntail"));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"one\n  two\ntail",
        {0, 0}));
    result = pressAscii(core, editor, QChar(u'd'));
    QVERIFY(result.inputHintGeneration);
    const auto enterHint = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(enterHint);
    QVERIFY(std::ranges::any_of(
        enterHint->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"<CR>"
                && candidate.completesMapping
                && !candidate.hasChildren;
        }));
    result = press(core, editor, Qt::Key_Return);
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"tail"));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));

    // Replacement arguments are likewise one grammar table: printable
    // characters, Enter, and Tab must all be advertised and executable.
    QVERIFY(core.replaceBufferText(
        buffer,
        u"ab",
        {0, 0}));
    result = pressAscii(core, editor, QChar(u'r'));
    QVERIFY(result.inputHintGeneration);
    const auto replacement = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(replacement);
    QCOMPARE(
        replacement->prefixNotation,
        std::u32string(U"r"));
    QCOMPARE(
        replacement->candidates.size(),
        std::size_t{3});
    const auto replacementCandidate =
        [&replacement](const std::u32string_view key) {
            return std::ranges::find_if(
                replacement->candidates,
                [key](
                    const InputHintCandidate
                        &candidate) {
                    return candidate.keyNotation == key;
                });
        };
    for (const std::u32string_view key :
         {std::u32string_view{U"<char>"},
          std::u32string_view{U"<CR>"},
          std::u32string_view{U"<Tab>"}}) {
        const auto candidate =
            replacementCandidate(key);
        QVERIFY(candidate
                != replacement->candidates.cend());
        QVERIFY(candidate->completesMapping);
        QVERIFY(!candidate->hasChildren);
        QCOMPARE(
            candidate->origin,
            InputHintOrigin::NativeGrammar);
    }
    result = press(core, editor, Qt::Key_Return);
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"\nb"));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"ab",
        {0, 0}));
    result = pressAscii(core, editor, QChar(u'r'));
    QVERIFY(result.inputHintGeneration);
    result = press(core, editor, Qt::Key_Tab);
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"\tb"));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"ab",
        {0, 0}));
    result = pressAscii(core, editor, QChar(u'r'));
    QVERIFY(result.inputHintGeneration);
    result = pressAscii(core, editor, QChar(u'x'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"xb"));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
}

void VkCoreTests::
    nativeWindowGrammarSharesDiscoveryExecutionAndAliases()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Navigation);
    const auto pressWindowPrefix = [&core, window] {
        return press(
            core,
            window,
            Qt::Key_W,
            vimControlModifier());
    };
    const auto pressDescriptor =
        [&core, window](const WindowCommandDescriptor &descriptor) {
            const bool upper = descriptor.key >= U'A'
                && descriptor.key <= U'Z';
            const int key = descriptor.key >= U'a'
                    && descriptor.key <= U'z'
                ? Qt::Key_A
                    + static_cast<int>(
                        descriptor.key - U'a')
                : descriptor.key >= U'A'
                        && descriptor.key <= U'Z'
                ? Qt::Key_A
                    + static_cast<int>(
                        descriptor.key - U'A')
                : static_cast<int>(descriptor.key);
            return press(
                core,
                window,
                key,
                upper ? Qt::ShiftModifier
                      : Qt::NoModifier,
                QString(QChar(
                    static_cast<char16_t>(descriptor.key))));
        };
    const auto settleHostBarrier =
        [&core, window](const DispatchResult &result) {
            if (!result.hostBarrierGeneration) {
                return;
            }
            const DispatchResult resumed =
                core.acknowledgeHostBarrier(
                    *result.hostBarrierGeneration,
                    true,
                    window,
                    window);
            Q_ASSERT(!resumed.hostBarrierGeneration.has_value());
        };

    for (const WindowCommandDescriptor &descriptor :
         WindowCommands) {
        const DispatchResult prefix = pressWindowPrefix();
        QCOMPARE(prefix.disposition, InputDisposition::Pending);
        QVERIFY(prefix.inputHintGeneration.has_value());
        const auto snapshot = core.pendingInputHint(
            *prefix.inputHintGeneration);
        QVERIFY(snapshot.has_value());
        QCOMPARE(snapshot->prefixNotation, std::u32string(U"<C-w>"));
        const auto candidate = std::ranges::find_if(
            snapshot->candidates,
            [&descriptor](const InputHintCandidate &value) {
                return value.keyNotation == descriptor.notation;
            });
        QVERIFY(candidate != snapshot->candidates.cend());
        QCOMPARE(candidate->description,
                 std::string(descriptor.description));
        QCOMPARE(candidate->commandId,
                 std::string(descriptor.commandId));

        const DispatchResult executed =
            pressDescriptor(descriptor);
        QVERIFY(hasCommandRequest(
            executed, descriptor.commandId));
        settleHostBarrier(executed);
    }

    // A count belongs to the complete window operation, not the prefix or
    // whichever Qt widget receives its continuation.
    auto result = pressAscii(core, window, QChar(u'3'));
    result = pressWindowPrefix();
    result = pressAscii(core, window, QChar(u'l'));
    const auto counted = std::ranges::find_if(
        result.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested
                && event.commandId == WindowFocusRightCommand;
        });
    QVERIFY(counted != result.events.cend());
    QCOMPARE(counted->count, std::size_t{3});
    QVERIFY(counted->countWasExplicit);
    settleHostBarrier(result);

    struct Alias final
    {
        int key = 0;
        Qt::KeyboardModifiers modifiers;
        std::string_view command;
    };
    const Qt::KeyboardModifiers control =
        vimControlModifier();
    const std::array aliases{
        Alias{Qt::Key_W, control, WindowFocusNextCommand},
        Alias{Qt::Key_H, control, WindowFocusLeftCommand},
        Alias{Qt::Key_J, control, WindowFocusDownCommand},
        Alias{Qt::Key_K, control, WindowFocusUpCommand},
        Alias{Qt::Key_L, control, WindowFocusRightCommand},
        Alias{Qt::Key_Backspace, Qt::NoModifier,
              WindowFocusLeftCommand},
        Alias{Qt::Key_Left, Qt::NoModifier,
              WindowFocusLeftCommand},
        Alias{Qt::Key_Down, Qt::NoModifier,
              WindowFocusDownCommand},
        Alias{Qt::Key_Up, Qt::NoModifier,
              WindowFocusUpCommand},
        Alias{Qt::Key_Right, Qt::NoModifier,
              WindowFocusRightCommand},
        Alias{Qt::Key_T, control,
              WindowFocusFirstCommand},
        Alias{Qt::Key_B, control,
              WindowFocusLastCommand},
        Alias{Qt::Key_P, control,
              WindowFocusPreviouslyAccessedCommand},
        Alias{Qt::Key_S, control,
              WindowSplitHorizontalCommand},
        Alias{Qt::Key_V, control,
              WindowSplitVerticalCommand},
        Alias{Qt::Key_Q, control,
              WindowQuitCommand},
        Alias{Qt::Key_Underscore, control,
              WindowMaximizeHeightCommand},
    };
    for (const Alias &alias : aliases) {
        result = pressWindowPrefix();
        QCOMPARE(result.disposition, InputDisposition::Pending);
        result = press(
            core,
            window,
            alias.key,
            alias.modifiers);
        QVERIFY(hasCommandRequest(result, alias.command));
        settleHostBarrier(result);
    }

    // Topology mutations need a renderer/model transaction that can commit
    // atomically across arbitrary panel providers. Until that boundary is
    // implemented, never advertise commands which the host would reject.
    struct UnsupportedWindowKey final
    {
        int key = 0;
        Qt::KeyboardModifiers modifiers;
        std::u32string_view notation;
    };
    const std::array unsupportedWindowKeys{
        UnsupportedWindowKey{Qt::Key_H, Qt::ShiftModifier, U"H"},
        UnsupportedWindowKey{Qt::Key_J, Qt::ShiftModifier, U"J"},
        UnsupportedWindowKey{Qt::Key_K, Qt::ShiftModifier, U"K"},
        UnsupportedWindowKey{Qt::Key_L, Qt::ShiftModifier, U"L"},
        UnsupportedWindowKey{Qt::Key_O, Qt::NoModifier, U"o"},
        UnsupportedWindowKey{Qt::Key_X, Qt::NoModifier, U"x"},
        UnsupportedWindowKey{Qt::Key_R, Qt::NoModifier, U"r"},
        UnsupportedWindowKey{Qt::Key_R, Qt::ShiftModifier, U"R"},
    };
    for (const UnsupportedWindowKey &unsupported :
         unsupportedWindowKeys) {
        result = pressWindowPrefix();
        QVERIFY(result.inputHintGeneration.has_value());
        const auto snapshot = core.pendingInputHint(
            *result.inputHintGeneration);
        QVERIFY(snapshot.has_value());
        QVERIFY(std::ranges::none_of(
            snapshot->candidates,
            [&unsupported](const InputHintCandidate &candidate) {
                return candidate.keyNotation
                    == unsupported.notation;
            }));
        result = press(
            core,
            window,
            unsupported.key,
            unsupported.modifiers,
            QString(QChar(
                static_cast<char16_t>(
                    unsupported.notation.front()))));
        QVERIFY(std::ranges::any_of(
            result.events,
            [](const Event &event) {
                return event.type == EventType::InputError
                    && event.message
                        == "unsupported CTRL-W command";
            }));
        QVERIFY(std::ranges::none_of(
            result.events,
            [](const Event &event) {
                return event.type
                    == EventType::CommandRequested;
            }));
    }
    for (const int unsupportedControlAlias : {
             Qt::Key_O,
             Qt::Key_X,
             Qt::Key_R}) {
        result = pressWindowPrefix();
        result = press(
            core,
            window,
            unsupportedControlAlias,
            control);
        QVERIFY(std::ranges::any_of(
            result.events,
            [](const Event &event) {
                return event.type == EventType::InputError
                    && event.message
                        == "unsupported CTRL-W command";
            }));
        QVERIFY(std::ranges::none_of(
            result.events,
            [](const Event &event) {
                return event.type
                    == EventType::CommandRequested;
            }));
    }

    // Neovim treats CTRL-C as prefix cancellation. It must never alias the
    // printable `c` command that closes a window.
    result = pressWindowPrefix();
    result = press(
        core, window, Qt::Key_C, control);
    QVERIFY(std::ranges::none_of(
        result.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested
                && event.commandId == WindowCloseCommand;
        }));
    QVERIFY(!core.pendingInputHint(
        result.inputHintGeneration.value_or(0)));

    // CTRL-W n/CTRL-N needs an authoritative unnamed-buffer host before it
    // can match :new. Until that boundary exists both spellings fail
    // explicitly; CTRL-N must not leak through as the ordinary line motion.
    for (const bool controlN : {false, true}) {
        result = pressWindowPrefix();
        result = controlN
            ? press(core, window, Qt::Key_N, control)
            : pressAscii(core, window, QChar(u'n'));
        QVERIFY(std::ranges::any_of(
            result.events,
            [](const Event &event) {
                return event.type == EventType::InputError
                    && event.message
                        == "unsupported CTRL-W command";
            }));
        QVERIFY(std::ranges::none_of(
            result.events,
            [](const Event &event) {
                return event.type == EventType::CommandRequested;
            }));
    }
}

void VkCoreTests::longestMatchWaitsAndNowaitDoesNot()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    std::string error;
    QVERIFY(core.addMapping(
        actionMapping(U"q", HostAction::FocusPanelLeft),
        &error) != 0);
    QVERIFY(core.addMapping(
        actionMapping(U"qq", HostAction::FocusPanelRight),
        &error) != 0);

    auto result = pressAscii(core, editor, QChar(u'q'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    const std::uint64_t generation =
        *result.mappingDeadlineGeneration;
    result = core.mappingTimedOut(generation);
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelLeft));

    result = pressAscii(core, editor, QChar(u'q'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, editor, QChar(u'q'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));

    QVERIFY(core.addMapping(
        actionMapping(
            U"z",
            HostAction::FocusPanelUp,
            mappingModes(MappingMode::Normal),
            true),
        &error) != 0);
    QVERIFY(core.addMapping(
        actionMapping(U"zz", HostAction::FocusPanelDown),
        &error) != 0);
    result = pressAscii(core, editor, QChar(u'z'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = core.mappingTimedOut(
        *result.mappingDeadlineGeneration);
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelUp));

    // <nowait> only wins when its full match is encountered before a
    // partial longer mapping, matching Neovim's map-list ordering.
    QVERIFY(core.addMapping(
        actionMapping(U"yy", HostAction::FocusPanelDown),
        &error) != 0);
    QVERIFY(core.addMapping(
        actionMapping(
            U"y",
            HostAction::FocusPanelUp,
            mappingModes(MappingMode::Normal),
            true),
        &error) != 0);
    result = pressAscii(core, editor, QChar(u'y'));
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelUp));
}

void VkCoreTests::recursiveAndNoremapRhsUseTypeaheadFlags()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    std::string error;
    QVERIFY(core.addMapping(
        actionMapping(U"m", HostAction::FocusPanelRight),
        &error) != 0);
    QVERIFY(core.addMapping(
        keyMapping(U"r", U"m"),
        &error) != 0);

    auto result = pressAscii(core, editor, QChar(u'r'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));

    QVERIFY(core.addMapping(
        keyMapping(U"n", U"m", RemapPolicy::NoRemap),
        &error) != 0);
    result = pressAscii(core, editor, QChar(u'n'));
    QVERIFY(!hasHostAction(
        result, HostAction::FocusPanelRight));
}

void VkCoreTests::failedPrefixPreservesEveryTypedKey()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"abcdef",
        {0, 0}) != 0);
    std::string error;
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"<Leader>e",
                HostAction::Cancel),
            &error)
            != 0,
        error.c_str());

    // Space starts <Leader> mappings. On mismatch it is emitted first as the
    // normal-mode space command, then l is emitted; both move right.
    auto result = pressAscii(core, editor, QChar(u' '));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, editor, QChar(u'l'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 2}));
}

void VkCoreTests::recursiveCycleIsBounded()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    std::string error;
    QVERIFY(core.addMapping(keyMapping(U"r", U"s"), &error) != 0);
    QVERIFY(core.addMapping(keyMapping(U"s", U"r"), &error) != 0);
    const DispatchResult result =
        pressAscii(core, editor, QChar(u'r'));
    const bool reported = std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::InputError
                && event.message.find("maxmapdepth")
                       != std::string::npos;
        });
    QVERIFY(reported);
}

void VkCoreTests::recursiveMappingWithLiteralProgressCannotBlockHost()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"abcdefghijklmnopqrstuvwxyz",
        {0, 20}) != 0);
    std::string error;
    QVERIFY(core.addMapping(
        keyMapping(U"h", U"hh"),
        &error) != 0);

    // Neovim's Vi-compatible prefix rule emits the first RHS h literally and
    // remaps the second. Unlike Neovim's interruptible editor loop, vkery
    // drains one Qt input transaction synchronously, so a second chain budget
    // must prevent this productive recursion from monopolizing the GUI thread.
    const DispatchResult result =
        pressAscii(core, editor, QChar(u'h'));
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::InputError
                && event.message.find("maxmapdepth")
                       != std::string::npos;
        }));
    QVERIFY(result.events.size() <= 1'001);
}

void VkCoreTests::operatorPendingIsARealMappingMode()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"one two",
        {0, 0});
    QVERIFY(buffer != 0);
    std::string error;
    QVERIFY(core.addMapping(
        actionMapping(
            U"q",
            HostAction::FocusPanelRight,
            mappingModes(MappingMode::OperatorPending)),
        &error) != 0);

    auto result = pressAscii(core, editor, QChar(u'd'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::OperatorPending));
    result = pressAscii(core, editor, QChar(u'q'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::ModeChanged
                && event.mode == Mode::Normal;
        }));

    result = press(
        core, editor, Qt::Key_Escape);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    result = pressAscii(core, editor, QChar(u'd'));
    result = pressAscii(core, editor, QChar(u'd'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string{});

    QVERIFY(core.replaceBufferText(
        buffer, u"one two", {0, 0}));
    result = pressAscii(core, editor, QChar(u'd'));
    result = pressAscii(core, editor, QChar(u'w'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"two"));
}

void VkCoreTests::hostActionMappingsTerminateNormalGrammar()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"one\ntwo\nthree\nfour",
        {0, 0}) != 0);
    std::string error;
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"<Leader>e",
                HostAction::Cancel),
            &error)
            != 0,
        error.c_str());

    // A count applies to the command it prefixes, not to a later motion after
    // a mapped host action has completed.
    auto result = pressAscii(core, editor, QChar(u'2'));
    result = pressAscii(core, editor, QChar(u' '));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, editor, QChar(u'e'));
    QVERIFY(hasHostAction(
        result, HostAction::Cancel));
    result = pressAscii(core, editor, QChar(u'j'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{1, 0}));

    // Literal Normal prefixes are command-local too. After the host action,
    // the first g starts a new prefix instead of completing the old one.
    result = pressAscii(core, editor, QChar(u'g'));
    result = pressAscii(core, editor, QChar(u' '));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, editor, QChar(u'e'));
    QVERIFY(hasHostAction(
        result, HostAction::Cancel));
    result = pressAscii(core, editor, QChar(u'g'));
    QVERIFY(!lastCursor(result).has_value());
}

void VkCoreTests::bufferLocalMappingPrecedesGlobalMapping()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"text",
        {0, 0});
    QVERIFY(buffer != 0);
    std::string error;
    QVERIFY(core.addMapping(
        actionMapping(U"z", HostAction::FocusPanelLeft),
        &error) != 0);
    QVERIFY(core.addMapping(
        actionMapping(
            U"z",
            HostAction::FocusPanelRight,
            mappingModes(MappingMode::Normal),
            false,
            buffer),
        &error) != 0);
    const DispatchResult result =
        pressAscii(core, editor, QChar(u'z'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));
    QVERIFY(!hasHostAction(
        result, HostAction::FocusPanelLeft));
}

void VkCoreTests::
    windowLocalMappingPrecedesBufferAndGlobal()
{
    VkCore core;
    const WindowId editor =
        core.registerWindow(WindowKind::Editor);
    const WindowId surface =
        core.registerWindow(WindowKind::Surface);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"text",
        {0, 0});
    QVERIFY(buffer != 0);

    std::string error;
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"q",
                HostAction::FocusPanelLeft),
            &error)
            != 0,
        error.c_str());
    MappingDefinition bufferLocal = actionMapping(
        U"q",
        HostAction::FocusPanelDown);
    bufferLocal.buffer = buffer;
    QVERIFY2(
        core.addMapping(bufferLocal, &error) != 0,
        error.c_str());
    MappingDefinition windowLocal = actionMapping(
        U"q",
        HostAction::FocusPanelRight);
    windowLocal.window = surface;
    QVERIFY2(
        core.addMapping(windowLocal, &error) != 0,
        error.c_str());

    DispatchResult result =
        pressAscii(core, surface, QChar(u'q'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));
    result = pressAscii(core, editor, QChar(u'q'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelDown));

    core.unregisterWindow(surface);
    QCOMPARE(
        core.addMapping(windowLocal, &error),
        MappingId{0});
    QCOMPARE(
        error,
        std::string(
            "window-local mapping requires a live window"));
}

void VkCoreTests::
    windowLocalMappingShadowsNativeCandidateAndPreservesFallback()
{
    VkCore core;
    const WindowId scoped =
        core.registerWindow(WindowKind::Editor);
    const WindowId peer =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        scoped,
        "/vault/mapping-native-overlap.txt",
        u"zero   \none",
        {0, 0});
    QVERIFY(buffer != 0);
    QVERIFY(core.attachBuffer(
        peer, buffer, Cursor{1, 0}));

    MappingDefinition local = actionMapping(
        U"gg", HostAction::FocusPanelRight);
    local.window = scoped;
    local.metadata = MappingMetadata{
        "First filesystem item",
        "Navigation",
        "vkery.vktree",
        false,
        120};
    std::string error;
    const MappingId localId =
        core.addMapping(local, &error);
    QVERIFY2(localId != 0, error.c_str());

    auto result =
        pressAscii(core, scoped, QChar(u'g'));
    QCOMPARE(
        result.disposition,
        InputDisposition::Pending);
    QVERIFY(result.inputHintGeneration);
    QVERIFY(!result.mappingDeadlineGeneration);
    const auto scopedHint = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(scopedHint);
    QCOMPARE(
        scopedHint->waitPolicy,
        InputHintWaitPolicy::PersistentGrammar);
    QVERIFY(scopedHint->nativeFallbackReachable);
    QCOMPARE(
        scopedHint->prefixNotation,
        std::u32string(U"g"));
    QVERIFY(scopedHint->candidates.front().windowLocal);
    QCOMPARE(
        scopedHint->candidates.front().origin,
        InputHintOrigin::Mapping);
    QCOMPARE(
        std::ranges::count_if(
            scopedHint->candidates,
            [](const InputHintCandidate &candidate) {
                return candidate.keyNotation == U"g";
            }),
        std::ptrdiff_t{1});
    const auto mappedGo = std::ranges::find_if(
        scopedHint->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"g";
        });
    QVERIFY(mappedGo != scopedHint->candidates.cend());
    QCOMPARE(mappedGo->origin, InputHintOrigin::Mapping);
    QVERIFY(mappedGo->windowLocal);
    QCOMPARE(mappedGo->mappingId, localId);
    QCOMPARE(
        mappedGo->description,
        std::string("First filesystem item"));
    QVERIFY(std::ranges::any_of(
        scopedHint->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"_"
                && candidate.origin
                    == InputHintOrigin::NativeGrammar
                && candidate.mappingId == 0
                && candidate.description
                    == "Last non-blank character";
        }));

    // `_` misses the local mapping trie, so the pending literal `g` enters
    // native grammar and completes g_.
    result = press(
        core,
        scoped,
        Qt::Key_Underscore,
        Qt::NoModifier,
        QStringLiteral("_"));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 3}));
    QVERIFY(!hasHostAction(
        result, HostAction::FocusPanelRight));

    // The mapped child owns `gg`; native `gg` must not also execute.
    result = pressAscii(core, scoped, QChar(u'g'));
    result = pressAscii(core, scoped, QChar(u'g'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));
    QCOMPARE(
        core.window(scoped)->cursor,
        (Cursor{0, 3}));

    // The peer has no window-local mapping and sees the native persistent node.
    result = pressAscii(core, peer, QChar(u'g'));
    QVERIFY(result.inputHintGeneration);
    QVERIFY(!result.mappingDeadlineGeneration);
    const auto peerHint = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(peerHint);
    QCOMPARE(
        peerHint->waitPolicy,
        InputHintWaitPolicy::PersistentGrammar);
    QVERIFY(std::ranges::all_of(
        peerHint->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.origin
                == InputHintOrigin::NativeGrammar;
        }));
    result = pressAscii(core, peer, QChar(u'g'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 0}));
}

void VkCoreTests::
    exactShortMappingSuppressesUnreachableNativeFallback()
{
    VkCore core;
    const WindowId editor =
        core.registerWindow(WindowKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/exact-mapping.txt",
        u"zero   \none",
        {0, 0}) != 0);

    MappingDefinition exact = actionMapping(
        U"g", HostAction::FocusPanelLeft);
    exact.window = editor;
    MappingDefinition longer = actionMapping(
        U"gq", HostAction::FocusPanelRight);
    longer.window = editor;
    std::string error;
    QVERIFY2(
        core.addMapping(exact, &error) != 0,
        error.c_str());
    QVERIFY2(
        core.addMapping(longer, &error) != 0,
        error.c_str());

    auto result =
        pressAscii(core, editor, QChar(u'g'));
    QVERIFY(result.inputHintGeneration);
    QVERIFY(result.mappingDeadlineGeneration);
    const auto hint = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(hint);
    QVERIFY(!hint->nativeFallbackReachable);
    QVERIFY(std::ranges::none_of(
        hint->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.origin
                == InputHintOrigin::NativeGrammar;
        }));
    QVERIFY(std::ranges::none_of(
        hint->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"_";
        }));

    // A mismatch commits the exact mapping before the mismatching key. Native
    // g_ is therefore unreachable and must not have been advertised.
    result = press(
        core,
        editor,
        Qt::Key_Underscore,
        Qt::NoModifier,
        QStringLiteral("_"));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelLeft));
    QCOMPARE(
        core.window(editor)->cursor,
        (Cursor{0, 0}));
}

void VkCoreTests::
    localShortMappingWaitsForLongerGlobalUnlessNowait()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"text",
        {0, 0});
    QVERIFY(buffer != 0);
    std::string error;
    QVERIFY(core.addMapping(
        actionMapping(U"ab", HostAction::FocusPanelLeft),
        &error) != 0);
    QVERIFY(core.addMapping(
        actionMapping(
            U"a",
            HostAction::FocusPanelRight,
            mappingModes(MappingMode::Normal),
            false,
            buffer),
        &error) != 0);

    auto result = pressAscii(core, editor, QChar(u'a'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, editor, QChar(u'b'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelLeft));
    QVERIFY(!hasHostAction(
        result, HostAction::FocusPanelRight));

    QVERIFY(core.addMapping(
        actionMapping(
            U"a",
            HostAction::FocusPanelUp,
            mappingModes(MappingMode::Normal),
            true,
            buffer),
        &error) != 0);
    result = pressAscii(core, editor, QChar(u'a'));
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelUp));
}

void VkCoreTests::
    mappingRedefinitionDoesNotResurrectOverlappingModes()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"text",
        {0, 0}) != 0);
    std::string error;
    const MappingModes normalAndVisual =
        MappingMode::Normal | MappingMode::Visual;
    const MappingId original = core.addMapping(
        actionMapping(
            U"q",
            HostAction::FocusPanelLeft,
            normalAndVisual),
        &error);
    QVERIFY2(original != 0, error.c_str());
    const MappingId replacement = core.addMapping(
        actionMapping(U"q", HostAction::FocusPanelRight),
        &error);
    QVERIFY2(replacement != 0, error.c_str());

    auto result = pressAscii(core, editor, QChar(u'q'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));
    QVERIFY(!hasHostAction(
        result, HostAction::FocusPanelLeft));

    QVERIFY(core.removeMapping(replacement));
    result = pressAscii(core, editor, QChar(u'q'));
    QVERIFY(!hasHostAction(
        result, HostAction::FocusPanelLeft));
    QVERIFY(!hasHostAction(
        result, HostAction::FocusPanelRight));

    // `q` is the native macro-recording prefix once no mapping shadows it.
    // Cancel that legitimate pending grammar before testing Visual-mode
    // resolution; the removed mapping must not be resurrected in either
    // mode.
    (void)press(
        core,
        editor,
        Qt::Key_Escape);

    result = pressAscii(core, editor, QChar(u'v'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Visual));
    result = pressAscii(core, editor, QChar(u'q'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelLeft));
}

void VkCoreTests::mappingsCannotReplaceAnotherPluginOwner()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"text",
        {0, 0}) != 0);

    MappingDefinition first = actionMapping(
        U"q", HostAction::FocusPanelLeft);
    first.metadata.sourcePlugin = "plugin.one";
    std::string error;
    QVERIFY2(core.addMapping(first, &error) != 0, error.c_str());

    MappingDefinition foreign = actionMapping(
        U"q", HostAction::FocusPanelRight);
    foreign.metadata.sourcePlugin = "plugin.two";
    QCOMPARE(core.addMapping(foreign, &error), MappingId{0});
    QVERIFY(error.find("plugin.one") != std::string::npos);
    QVERIFY(error.find("plugin.two") != std::string::npos);
    auto result = pressAscii(core, editor, QChar(u'q'));
    QVERIFY(hasHostAction(result, HostAction::FocusPanelLeft));

    MappingDefinition sameOwner = actionMapping(
        U"q", HostAction::FocusPanelUp);
    sameOwner.metadata.sourcePlugin = "plugin.one";
    QVERIFY2(
        core.addMapping(sameOwner, &error) != 0,
        error.c_str());
    result = pressAscii(core, editor, QChar(u'q'));
    QVERIFY(hasHostAction(result, HostAction::FocusPanelUp));
}

void VkCoreTests::
    mappingBatchValidatesEveryLiveScopeWithoutConsumingIds()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/live.txt",
        u"text",
        {0, 0});
    QVERIFY(buffer != 0);

    MappingDefinition valid = actionMapping(
        U"a", HostAction::FocusPanelLeft);
    MappingDefinition staleWindow = actionMapping(
        U"b", HostAction::FocusPanelRight);
    staleWindow.window = WindowId{999'999};
    std::string error;
    QVERIFY(core.addMappings(
        std::array{valid, staleWindow}, &error).empty());
    QCOMPARE(
        error,
        std::string(
            "window-local mapping requires a live window"));

    // The rejected batch never published its valid prefix and did not
    // advance the mapping-id sequence.
    const MappingId first = core.addMapping(valid, &error);
    QCOMPARE(first, MappingId{1});

    MappingDefinition windowLocal = actionMapping(
        U"b", HostAction::FocusPanelRight);
    windowLocal.window = window;
    MappingDefinition bufferLocal = actionMapping(
        U"c", HostAction::FocusPanelDown);
    bufferLocal.buffer = buffer;
    const auto installed = core.addMappings(
        std::array{windowLocal, bufferLocal}, &error);
    QCOMPARE(installed, std::vector<MappingId>({2, 3}));

    QVERIFY(core.removeBuffer(buffer));
    QVERIFY(core.addMapping(bufferLocal, &error) == 0);
    QCOMPARE(
        error,
        std::string(
            "buffer-local mapping requires a live buffer"));
    core.unregisterWindow(window);
    QVERIFY(core.addMapping(windowLocal, &error) == 0);
    QCOMPARE(
        error,
        std::string(
            "window-local mapping requires a live window"));

    MappingDefinition afterFailures = actionMapping(
        U"d", HostAction::FocusPanelUp);
    // Buffer destruction revoked id 3 but ids are capabilities and are never
    // recycled; the two scope-validation failures also consume nothing.
    QCOMPARE(
        core.addMapping(afterFailures, &error),
        MappingId{4});
}

void VkCoreTests::insertTextPassesThroughAndEscapeReturnsToNormal()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"alpha",
        {0, 0}) != 0);
    MappingDefinition preferences = actionMapping(
        U"<C-,>",
        HostAction::ShowPreferences);
    preferences.modes =
        MappingMode::Normal
        | MappingMode::Insert
        | MappingMode::Visual
        | MappingMode::OperatorPending;
    std::string error;
    QVERIFY2(
        core.addMapping(preferences, &error) != 0,
        error.c_str());
    preferences.lhs = U"<D-,>";
    QVERIFY2(
        core.addMapping(preferences, &error) != 0,
        error.c_str());

    auto result = pressAscii(core, editor, QChar(u'i'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));
    const QKeyEvent textEvent(
        QEvent::KeyPress,
        Qt::Key_A,
        Qt::NoModifier,
        QStringLiteral("a"));
    QVERIFY(!core.shouldCapture(editor, textEvent));
    QCOMPARE(
        core.dispatch(
            editor, editor, textEvent).disposition,
        InputDisposition::PassThrough);

    result = press(
        core,
        editor,
        Qt::Key_Comma,
        Qt::ControlModifier,
        {});
    QVERIFY(hasHostAction(
        result, HostAction::ShowPreferences));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));

    result = press(core, editor, Qt::Key_Escape);
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));

    result = pressAscii(core, editor, QChar(u'i'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));
    const QKeyEvent controlC(
        QEvent::KeyPress,
        Qt::Key_C,
        vimControlModifier(),
        {});
    QVERIFY(core.shouldCapture(editor, controlC));
    result = core.dispatch(editor, editor, controlC);
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
}

void VkCoreTests::insertMappingsReplayTextWithoutLosingPrefixes()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"alpha",
        {0, 0}) != 0);
    std::string error;
    QVERIFY(core.addMapping(
        keyMapping(
            U"jk",
            U"<Esc>",
            RemapPolicy::NoRemap,
            mappingModes(MappingMode::Insert)),
        &error) != 0);
    QVERIFY(core.addMapping(
        keyMapping(
            U"q",
            U"λ",
            RemapPolicy::NoRemap,
            mappingModes(MappingMode::Insert)),
        &error) != 0);

    auto result = pressAscii(core, editor, QChar(u'i'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));

    result = pressAscii(core, editor, QChar(u'j'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, editor, QChar(u'x'));
    // Only the queued prefix is replayed by VK. The mismatching physical x
    // remains a native Qt Insert event instead of being synthesized by the
    // core.
    QCOMPARE(result.disposition, InputDisposition::PassThrough);
    QCOMPARE(insertedText(result), std::u16string(u"j"));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));

    result = pressAscii(core, editor, QChar(u'q'));
    QCOMPARE(insertedText(result), std::u16string(u"λ"));
    result = pressAscii(core, editor, QChar(u'j'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, editor, QChar(u'k'));
    QVERIFY(insertedText(result).empty());
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
}

void VkCoreTests::insertMappingsRejectUnsupportedAtomicEffects()
{
    VkCore core;
    std::string error;
    QCOMPARE(
        core.addMapping(
            keyMapping(
                U"q",
                U"λ<Esc>l",
                RemapPolicy::NoRemap,
                mappingModes(MappingMode::Insert)),
            &error),
        MappingId{0});
    QVERIFY(!error.empty());
    QCOMPARE(
        core.addMapping(
            keyMapping(
                U"q",
                U"<C-q>",
                RemapPolicy::NoRemap,
                mappingModes(MappingMode::Insert)),
            &error),
        MappingId{0});
    QVERIFY(!error.empty());
    QVERIFY(core.addMapping(
        keyMapping(
            U"jk",
            U"<Esc>",
            RemapPolicy::NoRemap,
            mappingModes(MappingMode::Insert)),
        &error) != 0);
}

void VkCoreTests::
    pendingInsertPrefixDoesNotSwallowNativeModifiedKey()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"alpha",
        {0, 0}) != 0);
    std::string error;
    QVERIFY(core.addMapping(
        keyMapping(
            U"jk",
            U"<Esc>",
            RemapPolicy::NoRemap,
            mappingModes(MappingMode::Insert)),
        &error) != 0);

    auto result = pressAscii(core, editor, QChar(u'i'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));
    result = pressAscii(core, editor, QChar(u'j'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    const std::uint64_t stale =
        *result.mappingDeadlineGeneration;

    const QKeyEvent selectLeft(
        QEvent::KeyPress,
        Qt::Key_Left,
        Qt::ShiftModifier);
    // ShortcutOverride must be claimed for this one transaction so KeyPress
    // can commit the pending literal before Qt applies Shift+Left. The
    // current key itself remains PassThrough below.
    QVERIFY(core.shouldCapture(editor, selectLeft));
    result = core.dispatch(editor, editor, selectLeft);
    QCOMPARE(result.disposition, InputDisposition::PassThrough);
    QCOMPARE(insertedText(result), std::u16string(u"j"));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));
    QVERIFY(core.mappingTimedOut(stale).events.empty());

    result = press(core, editor, Qt::Key_Escape);
    QVERIFY(core.addMapping(
        keyMapping(
            U"j",
            U"<Esc>",
            RemapPolicy::NoRemap,
            mappingModes(MappingMode::Insert)),
        &error) != 0);
    result = pressAscii(core, editor, QChar(u'i'));
    result = pressAscii(core, editor, QChar(u'j'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    // The short mapping exits Insert before Shift+Left is resolved. The
    // modifier key must therefore remain inside VK's Normal grammar instead
    // of leaking to Qt as a text selection.
    QVERIFY(core.shouldCapture(editor, selectLeft));
    result = core.dispatch(editor, editor, selectLeft);
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
}

void VkCoreTests::recursiveInsertProbeUsesTheRealResolver()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"alpha",
        {0, 0}) != 0);
    std::string error;
    const MappingModes insert =
        mappingModes(MappingMode::Insert);
    QVERIFY(core.addMapping(
        keyMapping(
            U"j", U"a", RemapPolicy::Recursive, insert),
        &error) != 0);
    QVERIFY(core.addMapping(
        keyMapping(
            U"jk", U"q", RemapPolicy::NoRemap, insert),
        &error) != 0);
    QVERIFY(core.addMapping(
        actionMapping(
            U"ax",
            HostAction::FocusPanelRight,
            insert),
        &error) != 0);

    auto result = pressAscii(core, editor, QChar(u'i'));
    result = pressAscii(core, editor, QChar(u'j'));
    QCOMPARE(result.disposition, InputDisposition::Pending);

    const QKeyEvent x(
        QEvent::KeyPress,
        Qt::Key_X,
        Qt::NoModifier,
        QStringLiteral("x"));
    QVERIFY(core.shouldCapture(editor, x));
    result = core.dispatch(editor, editor, x);
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));
    QVERIFY(insertedText(result).empty());
}

void VkCoreTests::nativeShortcutSettlesPrefixBeforeModeDecision()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"alpha",
        {0, 0}) != 0);
    std::string error;
    QVERIFY(core.addMapping(
        keyMapping(U"q", U"i"),
        &error) != 0);
    QVERIFY(core.addMapping(
        keyMapping(U"qq", U"x"),
        &error) != 0);

    auto result = pressAscii(core, editor, QChar(u'q'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    const std::uint64_t stale =
        *result.mappingDeadlineGeneration;
    const QKeyEvent paste(
        QEvent::KeyPress,
        Qt::Key_V,
        Qt::ControlModifier);
    QVERIFY(!core.pendingMappingConsumes(
        editor, paste));

    // q resolves through the real Normal grammar first and enters Insert.
    // The original platform paste key is then returned to Qt in the new
    // mode. On non-macOS this also proves that initial Normal-mode ownership
    // is not allowed to swallow Ctrl+V before q has changed the mode.
    result = core.dispatch(editor, editor, paste);
    QCOMPARE(result.disposition, InputDisposition::PassThrough);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::ModeChanged
                && event.mode == Mode::Insert;
        }));
    QVERIFY(core.mappingTimedOut(stale).events.empty());

    result = press(core, editor, Qt::Key_Escape);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    result = pressAscii(core, editor, QChar(u'q'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    const QKeyEvent selectLeft(
        QEvent::KeyPress,
        Qt::Key_Left,
        Qt::ShiftModifier);
    QVERIFY(!core.pendingMappingConsumes(
        editor, selectLeft));
    result = core.dispatch(
        editor, editor, selectLeft);
    QCOMPARE(result.disposition, InputDisposition::PassThrough);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));
}

void VkCoreTests::
    nativeWindowContinuationCannotBeStolenByLocalMapping()
{
    // First exercise the production shape: CTRL-W is purely native after
    // duplicate Lua mappings were removed, while vkFiles owns a local `l`.
    {
        VkCore core;
        const WindowId navigation =
            core.registerWindow(WindowKind::Navigation);
        std::string error;
        MappingDefinition localRight = actionMapping(
            U"l", HostAction::NavigateRight);
        localRight.window = navigation;
        QVERIFY2(
            core.addMapping(localRight, &error) != 0,
            error.c_str());

        auto result = press(
            core,
            navigation,
            Qt::Key_W,
            vimControlModifier());
        QCOMPARE(result.disposition, InputDisposition::Pending);
        QVERIFY(result.inputHintGeneration);
        QVERIFY(!result.mappingDeadlineGeneration);
        result = pressAscii(core, navigation, QChar(u'l'));
        QCOMPARE(result.disposition, InputDisposition::Consumed);
        QVERIFY(hasCommandRequest(
            result, WindowFocusRightCommand));
        QVERIFY(!hasHostAction(
            result, HostAction::NavigateRight));
    }

    // A user mapping sharing the CTRL-W prefix follows the same native
    // fallback transaction after its trie misses on the continuation.
    VkCore core;
    const WindowId navigation =
        core.registerWindow(WindowKind::Navigation);
    std::string error;

    MappingDefinition longerWindowMapping = actionMapping(
        U"<C-w>s", HostAction::FocusPanelDown);
    QVERIFY2(
        core.addMapping(longerWindowMapping, &error) != 0,
        error.c_str());

    MappingDefinition localRight = actionMapping(
        U"l", HostAction::NavigateRight);
    localRight.window = navigation;
    QVERIFY2(
        core.addMapping(localRight, &error) != 0,
        error.c_str());

    auto result = press(
        core,
        navigation,
        Qt::Key_W,
        vimControlModifier());
    QCOMPARE(result.disposition, InputDisposition::Pending);
    QVERIFY(result.inputHintGeneration);
    QVERIFY(!result.mappingDeadlineGeneration);
    const std::uint64_t stableGeneration =
        *result.inputHintGeneration;
    const auto beforeTimeout =
        core.pendingInputHint(stableGeneration);
    QVERIFY(beforeTimeout);
    QVERIFY(std::ranges::any_of(
        beforeTimeout->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"s"
                && candidate.origin
                    == InputHintOrigin::Mapping;
        }));
    QVERIFY(core.mappingTimedOut(stableGeneration).events.empty());
    const auto afterTimeout =
        core.pendingInputHint(stableGeneration);
    QVERIFY(afterTimeout);
    QCOMPARE(
        afterTimeout->candidates.size(),
        beforeTimeout->candidates.size());
    QVERIFY(std::ranges::any_of(
        afterTimeout->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"s"
                && candidate.origin
                    == InputHintOrigin::Mapping;
        }));

    result = pressAscii(core, navigation, QChar(u'l'));
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QCOMPARE(result.events.size(), std::size_t{1});
    QVERIFY(hasCommandRequest(
        result, WindowFocusRightCommand));
    QVERIFY(!hasHostAction(
        result, HostAction::NavigateRight));
}

void VkCoreTests::
    inputContextBoundariesDrainOrCancelPendingState()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const ViewId navigation =
        core.registerView(ViewKind::Navigation);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"alpha beta",
        {0, 2}) != 0);
    std::string error;
    QVERIFY(core.addMapping(
        keyMapping(
            U"jk",
            U"<Esc>",
            RemapPolicy::NoRemap,
            mappingModes(MappingMode::Insert)),
        &error) != 0);

    auto result = core.dispatch(
        editor,
        10,
        QKeyEvent(
            QEvent::KeyPress,
            Qt::Key_I,
            Qt::NoModifier,
            QStringLiteral("i")));
    result = core.dispatch(
        editor,
        10,
        QKeyEvent(
            QEvent::KeyPress,
            Qt::Key_J,
            Qt::NoModifier,
            QStringLiteral("j")));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    const std::uint64_t stale =
        *result.mappingDeadlineGeneration;

    result = core.transitionInputContext(editor, 20);
    QCOMPARE(insertedText(result), std::u16string(u"j"));
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::InsertText
                && event.inputTarget == 10;
        }));
    QVERIFY(core.mappingTimedOut(stale).events.empty());

    result = core.dispatch(
        editor,
        20,
        QKeyEvent(
            QEvent::KeyPress,
            Qt::Key_Escape,
            Qt::NoModifier));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    result = pressAscii(core, editor, QChar(u'g'));
    QVERIFY(result.events.empty());
    result = core.transitionInputContext(
        editor, editor, true);
    result = pressAscii(core, editor, QChar(u'h'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 1}));

    result = pressAscii(core, editor, QChar(u'd'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::OperatorPending));
    result = core.transitionInputContext(
        editor, editor, true);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::ModeChanged
                && event.mode == Mode::Normal;
        }));
    result = pressAscii(core, editor, QChar(u'w'));
    QVERIFY(core.buffer(
        core.activeBuffer(editor)->id)->text
            == std::u16string(u"alpha beta"));

    result = pressAscii(core, editor, QChar(u'i'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));
    result = core.transitionInputContext(
        navigation, navigation);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::ModeChanged
                && event.mode == Mode::Normal;
        }));
}

void VkCoreTests::navigationViewsCannotEnterOperatorPending()
{
    VkCore core;
    const ViewId navigation =
        core.registerView(ViewKind::Navigation);
    const DispatchResult result =
        pressAscii(core, navigation, QChar(u'd'));
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
}

void VkCoreTests::editorWithoutBufferCannotEnterModalState()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);

    for (const QChar key :
         {QChar(u'i'), QChar(u'a'), QChar(u'I'),
          QChar(u'A'), QChar(u'v'), QChar(u'd')}) {
        const DispatchResult result =
            pressAscii(core, editor, key);
        QCOMPARE(
            result.disposition,
            InputDisposition::Consumed);
        QCOMPARE(
            core.mode(),
            std::optional<Mode>(Mode::Normal));
    }

    const BufferId readOnly = core.synchronizeBuffer(
        editor,
        "/vault/locked.txt",
        u"text",
        {0, 0});
    QVERIFY(readOnly != 0);
    QVERIFY(core.setBufferReadOnly(
        readOnly, true));
    const DispatchResult result =
        pressAscii(core, editor, QChar(u'i'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::InputError
                && event.message.find("read-only")
                       != std::string::npos;
        }));
}

void VkCoreTests::
    surfaceViewsEmitNavigationAndPreserveNativeShortcuts()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const ViewId surface =
        core.registerView(ViewKind::Surface);
    std::string error;
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"<Leader>e",
                HostAction::Cancel),
            &error)
            != 0,
        error.c_str());
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"text",
        {0, 0}) != 0);
    (void)pressAscii(core, editor, QChar(u'i'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));
    (void)core.transitionInputContext(
        surface, surface, true);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));

    QVERIFY(hasHostAction(
        pressAscii(core, surface, QChar(u'h')),
        HostAction::NavigateLeft));
    QVERIFY(hasHostAction(
        pressAscii(core, surface, QChar(u'j')),
        HostAction::NavigateDown));
    QVERIFY(hasHostAction(
        pressAscii(core, surface, QChar(u'k')),
        HostAction::NavigateUp));
    QVERIFY(hasHostAction(
        pressAscii(core, surface, QChar(u'l')),
        HostAction::NavigateRight));
    QVERIFY(hasHostAction(
        press(core, surface, Qt::Key_Left),
        HostAction::NavigateLeft));
    QVERIFY(hasHostAction(
        press(core, surface, Qt::Key_Down),
        HostAction::NavigateDown));
    QVERIFY(hasHostAction(
        press(core, surface, Qt::Key_Up),
        HostAction::NavigateUp));
    QVERIFY(hasHostAction(
        press(core, surface, Qt::Key_Right),
        HostAction::NavigateRight));
    QVERIFY(hasHostAction(
        press(core, surface, Qt::Key_PageUp),
        HostAction::NavigateUp));
    QVERIFY(hasHostAction(
        press(core, surface, Qt::Key_PageDown),
        HostAction::NavigateDown));
    QVERIFY(hasHostAction(
        press(core, surface, Qt::Key_Backspace),
        HostAction::NavigateLeft));
    QVERIFY(hasHostAction(
        press(core, surface, Qt::Key_Return),
        HostAction::Activate));

    QVERIFY(core.setWindowViewport(surface, 0, 0, 10));
    DispatchResult result = press(
        core,
        surface,
        Qt::Key_D,
        vimControlModifier(),
        QStringLiteral("d"));
    const auto halfDown = std::ranges::find_if(
        result.events,
        [](const Event &event) {
            return event.type == EventType::HostAction
                && event.hostAction
                    == HostAction::NavigateHalfPageDown;
        });
    QVERIFY(halfDown != result.events.cend());
    QCOMPARE(halfDown->count, std::size_t{5});
    QCOMPARE(
        std::ranges::count_if(
            result.events,
            [](const Event &event) {
                return event.type == EventType::HostAction;
            }),
        std::ptrdiff_t{1});
    result = press(
        core,
        surface,
        Qt::Key_U,
        vimControlModifier(),
        QStringLiteral("u"));
    QVERIFY(hasHostAction(
        result, HostAction::NavigateHalfPageUp));

    result = pressAscii(core, surface, QChar(u'g'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, surface, QChar(u'g'));
    QVERIFY(hasHostAction(result, HostAction::NavigateFirst));
    result = pressAscii(core, surface, QChar(u'G'));
    QVERIFY(hasHostAction(result, HostAction::NavigateLast));

    result = pressAscii(core, surface, QChar(u' '));
    QCOMPARE(
        result.disposition,
        InputDisposition::Pending);
    QVERIFY(result.inputHintGeneration.has_value());
    QVERIFY(
        !result.mappingDeadlineGeneration.has_value());
    result = core.flushPendingInput();
    QVERIFY(hasHostAction(
        result, HostAction::NavigateRight));

    result = press(core, surface, Qt::Key_Escape);
    QVERIFY(hasHostAction(result, HostAction::Cancel));
    result = press(
        core,
        surface,
        Qt::Key_BracketLeft,
        vimControlModifier(),
        QStringLiteral("["));
    QVERIFY(hasHostAction(result, HostAction::Cancel));

    const QKeyEvent windowPrefix(
        QEvent::KeyPress,
        Qt::Key_W,
        vimControlModifier(),
        QStringLiteral("w"));
    QVERIFY(core.shouldCapture(
        surface, windowPrefix));
    result = core.dispatch(
        surface, surface, windowPrefix);
    QCOMPARE(
        result.disposition,
        InputDisposition::Pending);
    QVERIFY(result.inputHintGeneration.has_value());
    result = pressAscii(core, surface, QChar(u'h'));
    QVERIFY(hasCommandRequest(
        result, WindowFocusLeftCommand));

    const QKeyEvent annotationShortcut(
        QEvent::KeyPress,
        Qt::Key_N,
        Qt::ControlModifier | Qt::ShiftModifier,
        QStringLiteral("N"));
    QVERIFY(!core.shouldCapture(
        surface, annotationShortcut));
    result = core.dispatch(
        surface, surface, annotationShortcut);
    QCOMPARE(
        result.disposition,
        InputDisposition::PassThrough);

    // Global mappings still resolve on a Surface. <Leader> was expanded
    // when the mapping was defined; Space is not special-cased here.
    result = pressAscii(core, surface, QChar(u' '));
    QCOMPARE(
        result.disposition,
        InputDisposition::Pending);
    result = pressAscii(core, surface, QChar(u'e'));
    QVERIFY(hasHostAction(
        result, HostAction::Cancel));
}

void VkCoreTests::readOnlyBuffersRejectNormalEditsInCore()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/locked.txt",
        u"one two",
        {0, 0});
    QVERIFY(buffer != 0);
    QVERIFY(core.setBufferReadOnly(buffer, true));

    const DispatchResult result =
        pressAscii(core, editor, QChar(u'd'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"one two"));
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::InputError
                && event.message.find("read-only")
                       != std::string::npos;
        }));
}

void VkCoreTests::externalEditsUseUtf16AndRemainStrict()
{
    VkCore core;
    const ViewId firstView =
        core.registerView(ViewKind::Editor);
    const ViewId secondView =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        firstView,
        "/vault/utf16.txt",
        u"A😀B\nlast",
        {0, 4});
    QCOMPARE(
        core.synchronizeBuffer(
            secondView,
            "/vault/utf16.txt",
            u"A😀B\nlast",
            {1, 2}),
        buffer);
    const auto before = core.buffer(buffer);
    QVERIFY(before.has_value());

    // Offsets count UTF-16 code units: the emoji occupies two, while the
    // inserted newline must splice the line index for every attached view.
    QVERIFY(core.applyExternalEdit(
        firstView,
        1,
        2,
        u"中\n文",
        {1, 1}));
    const auto edited = core.buffer(buffer);
    QVERIFY(edited.has_value());
    QCOMPARE(
        edited->text,
        std::u16string(u"A中\n文B\nlast"));
    QCOMPARE(
        edited->revision,
        before->revision + 1);

    auto result =
        pressAscii(core, firstView, QChar(u'h'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{1, 0}));
    result =
        pressAscii(core, secondView, QChar(u'h'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{2, 1}));

    // Invalid host ranges fail atomically.
    QVERIFY(!core.applyExternalEdit(
        firstView,
        edited->text.size() + 1,
        0,
        u"!",
        {0, 0}));
    const auto afterInvalid = core.buffer(buffer);
    QVERIFY(afterInvalid.has_value());
    QCOMPARE(afterInvalid->text, edited->text);
    QCOMPARE(afterInvalid->revision, edited->revision);

    // QTextDocument can report format-only changes as an equal replacement.
    // They are successful no-ops and must not move the authoritative cursor.
    QVERIFY(core.applyExternalEdit(
        firstView,
        0,
        1,
        u"A",
        {2, 4}));
    const auto afterNoOp = core.buffer(buffer);
    QVERIFY(afterNoOp.has_value());
    QCOMPARE(afterNoOp->text, edited->text);
    QCOMPARE(afterNoOp->revision, edited->revision);
    result =
        pressAscii(core, firstView, QChar(u'h'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{1, 0}));
}

void VkCoreTests::bufferHistoryIsCoreOwnedAndExposesCleanState()
{
    VkCore core;
    core.setEnabled(false);
    const ViewId editor = core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/history.txt",
        u"one",
        {0, 3});
    QVERIFY(buffer != 0);

    auto history = core.bufferHistory(buffer);
    QVERIFY(history.has_value());
    QVERIFY(!history->canUndo);
    QVERIFY(!history->canRedo);
    QVERIFY(!history->modified());

    QVERIFY(core.applyExternalEdit(
        editor, 3, 0, u" two", {0, 7}));
    history = core.bufferHistory(buffer);
    QVERIFY(history->canUndo);
    QVERIFY(!history->canRedo);
    QVERIFY(history->modified());

    QVERIFY(core.applyExternalEditAtOffset(
        editor, 7, 0, u"\nline", 12));
    QCOMPARE(core.window(editor)->cursor, (Cursor{1, 4}));
    (void)core.undo(editor);
    QCOMPARE(core.buffer(buffer)->text, std::u16string(u"one two"));
    QCOMPARE(core.window(editor)->cursor, (Cursor{0, 7}));

    QVERIFY(core.setBufferModified(buffer, false));
    const std::uint64_t cleanNode =
        core.bufferHistory(buffer)->current;
    QVERIFY(!core.bufferHistory(buffer)->modified());

    QVERIFY(core.applyExternalEdit(
        editor, 7, 0, u" three", {0, 13}));
    QVERIFY(core.bufferHistory(buffer)->modified());
    (void)core.undo(editor);
    history = core.bufferHistory(buffer);
    QCOMPARE(history->current, cleanNode);
    QVERIFY(!history->modified());
    QVERIFY(history->canUndo);
    QVERIFY(history->canRedo);

    (void)core.redo(editor);
    history = core.bufferHistory(buffer);
    QVERIFY(history->modified());
    QVERIFY(history->canUndo);
    QVERIFY(!history->canRedo);

    QVERIFY(core.setBufferModified(buffer, true));
    history = core.bufferHistory(buffer);
    QVERIFY(!history->clean.has_value());
    QVERIFY(history->modified());
    QVERIFY(core.setBufferModified(buffer, false));
    QVERIFY(!core.bufferHistory(buffer)->modified());

    QVERIFY(!core.bufferHistory(BufferId{999}).has_value());
    QVERIFY(!core.setBufferModified(BufferId{999}, false));

    // A native host selection is Core state as well: the UTF-16 anchor and
    // active boundary survive undo/redo without a parallel widget stack.
    VkCore selectionCore;
    selectionCore.setEnabled(false);
    const ViewId selectionView =
        selectionCore.registerView(ViewKind::Editor);
    const BufferId selectionBuffer = selectionCore.synchronizeBuffer(
        selectionView,
        "/vault/selection.txt",
        u"A\U0001F600B",
        {0, 1});
    QVERIFY(selectionBuffer != 0);
    QVERIFY(selectionCore.setViewSelectionAtOffsets(
        selectionView, 1, 3));
    QCOMPARE(
        selectionCore.viewSelectionOffsets(selectionView),
        (std::optional<std::pair<std::size_t, std::size_t>>(
            std::pair<std::size_t, std::size_t>{1, 3})));
    QVERIFY(selectionCore.applyExternalEditWithSelectionAtOffsets(
        selectionView, 1, 2, u"x", 2, 2));
    QCOMPARE(
        selectionCore.buffer(selectionBuffer)->text,
        std::u16string(u"AxB"));
    (void)selectionCore.undo(selectionView);
    QCOMPARE(
        selectionCore.viewSelectionOffsets(selectionView),
        (std::optional<std::pair<std::size_t, std::size_t>>(
            std::pair<std::size_t, std::size_t>{1, 3})));
    (void)selectionCore.redo(selectionView);
    QCOMPARE(
        selectionCore.viewSelectionOffsets(selectionView),
        (std::optional<std::pair<std::size_t, std::size_t>>(
            std::pair<std::size_t, std::size_t>{2, 2})));
    QVERIFY(selectionCore.bufferHistory(selectionBuffer)->canUndo);
    QVERIFY(selectionCore.resetBufferHistory(selectionBuffer));
    QVERIFY(!selectionCore.bufferHistory(selectionBuffer)->canUndo);
    QVERIFY(!selectionCore.bufferHistory(selectionBuffer)->canRedo);
    QVERIFY(!selectionCore.bufferHistory(selectionBuffer)->modified());
    QVERIFY(!selectionCore.resetBufferHistory(BufferId{999}));
    QVERIFY(selectionCore.setViewSelectionAtOffsets(
        selectionView, 0, 2));
    selectionCore.setEnabled(true);
    QCOMPARE(
        selectionCore.viewSelectionOffsets(selectionView),
        (std::optional<std::pair<std::size_t, std::size_t>>(
            std::pair<std::size_t, std::size_t>{2, 2})));
}

void VkCoreTests::largeExternalEditsAvoidWholeBufferRescans()
{
    constexpr std::size_t documentSize =
        5 * 1024 * 1024;
    constexpr std::size_t editCount = 128;
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    std::u16string text(documentSize, u'a');
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/large.txt",
        std::move(text),
        {0, documentSize - 1});
    const auto before = core.buffer(buffer);
    QVERIFY(before.has_value());

    auto result =
        pressAscii(core, editor, QChar(u'a'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));
    QVERIFY(lastCursor(result).has_value());

    QElapsedTimer timer;
    timer.start();
    for (std::size_t index = 0;
         index < editCount;
         ++index) {
        QVERIFY(core.applyExternalEdit(
            editor,
            documentSize + index,
            0,
            u"x",
            {0, documentSize + index + 1}));
    }
    const qint64 editMilliseconds = timer.elapsed();
    QVERIFY2(
        editMilliseconds < 2'000,
        qPrintable(
            QStringLiteral(
                "128 incremental edits in a 5 MiB buffer took %1 ms")
                .arg(editMilliseconds)));

    const auto edited = core.buffer(buffer);
    QVERIFY(edited.has_value());
    QCOMPARE(
        edited->revision,
        before->revision + editCount);
    QCOMPARE(
        edited->text.size(),
        documentSize + editCount);
    QVERIFY(std::all_of(
        edited->text.cend()
            - static_cast<std::ptrdiff_t>(editCount),
        edited->text.cend(),
        [](const char16_t value) {
            return value == u'x';
        }));

    result = press(
        core,
        editor,
        Qt::Key_Escape);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    result =
        pressAscii(core, editor, QChar(u'h'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(
            Cursor{
                0,
                documentSize + editCount - 2}));
}

void VkCoreTests::displayLayoutCacheStaysSparseForHugeBuffers()
{
    constexpr std::size_t lineCount = 1'000'000;
    {
        VkCore core;
        const ViewId editor =
            core.registerView(ViewKind::Editor);
        std::u16string text;
        text.reserve(lineCount * 2 - 1);
        for (std::size_t line = 0; line < lineCount; ++line) {
            text.push_back(u'a');
            if (line + 1 != lineCount) {
                text.push_back(u'\n');
            }
        }
        const BufferId buffer = core.synchronizeBuffer(
            editor,
            "/vault/million-lines.txt",
            std::move(text),
            {lineCount / 2, 0});
        QCOMPARE(
            core.displayLayoutCacheStats(buffer),
            std::optional<DisplayLayoutCacheStats>(
                DisplayLayoutCacheStats{}));

        QElapsedTimer timer;
        timer.start();
        (void)press(
            core,
            editor,
            Qt::Key_V,
            vimControlModifier(),
            QStringLiteral("v"));
        (void)pressAscii(core, editor, QChar(u'l'));
        const auto state = core.window(editor);
        const qint64 elapsed = timer.elapsed();
        QVERIFY(state.has_value());
        QVERIFY2(
            elapsed < 1'000,
            qPrintable(QStringLiteral(
                "first local display query in one million lines took %1 ms")
                           .arg(elapsed)));
        QCOMPARE(
            core.displayLayoutCacheStats(buffer),
            std::optional<DisplayLayoutCacheStats>(
                DisplayLayoutCacheStats{1, 0, 0}));
    }

    constexpr std::size_t longLineSize = 5 * 1024 * 1024;
    {
        VkCore core;
        core.setVirtualEditMode(VirtualEditMode::Block);
        const ViewId editor =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            editor,
            "/vault/long-line.txt",
            std::u16string(longLineSize, u'a'),
            {0, 0});
        QCOMPARE(
            core.displayLayoutCacheStats(buffer),
            std::optional<DisplayLayoutCacheStats>(
                DisplayLayoutCacheStats{}));

        QElapsedTimer timer;
        timer.start();
        (void)press(
            core,
            editor,
            Qt::Key_V,
            vimControlModifier(),
            QStringLiteral("v"));
        (void)pressAscii(core, editor, QChar(u'l'));
        const auto state = core.window(editor);
        const qint64 elapsed = timer.elapsed();
        QVERIFY(state.has_value());
        QVERIFY2(
            elapsed < 1'000,
            qPrintable(QStringLiteral(
                "Visual Block on a 5 MiB ASCII line took %1 ms")
                           .arg(elapsed)));
        QCOMPARE(
            core.displayLayoutCacheStats(buffer),
            std::optional<DisplayLayoutCacheStats>(
                DisplayLayoutCacheStats{1, 0, 0}));

        for (const QChar digit : QStringLiteral("999999999")) {
            (void)pressAscii(core, editor, digit);
        }
        timer.restart();
        (void)pressAscii(core, editor, QChar(u'l'));
        const qint64 countElapsed = timer.elapsed();
        QVERIFY2(
            countElapsed < 100,
            qPrintable(QStringLiteral(
                "999999999l took %1 ms")
                           .arg(countElapsed)));
        const auto moved = core.window(editor);
        QVERIFY(moved.has_value());
        QCOMPARE(
            moved->displayCursor.column,
            std::size_t{1'000'000'000});
    }
}

void VkCoreTests::staleMappingTimeoutCannotReplayInput()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    std::string error;
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"<Leader>e",
                HostAction::Cancel),
            &error)
            != 0,
        error.c_str());
    auto result = pressAscii(core, editor, QChar(u' '));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    const std::uint64_t stale =
        *result.inputHintGeneration;
    QVERIFY(!result.mappingDeadlineGeneration.has_value());
    QVERIFY(
        core.mappingTimedOut(stale).events.empty());
    QVERIFY(core.pendingInputHint(stale).has_value());
    result = pressAscii(core, editor, QChar(u'e'));
    QVERIFY(hasHostAction(
        result, HostAction::Cancel));
    result = core.mappingTimedOut(stale);
    QCOMPARE(result.disposition, InputDisposition::PassThrough);
    QVERIFY(result.events.empty());
}

void VkCoreTests::
    mappingOwnerRevocationCancelsOnlyAffectedPendingPrefix()
{
    VkCore core;
    const ViewId editor = core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"text",
        {0, 0});
    QVERIFY(buffer != 0);

    std::string error;
    MappingDefinition windowMapping =
        actionMapping(U"dx", HostAction::FocusPanelLeft);
    windowMapping.window = editor;
    const MappingId affected = core.addMapping(
        windowMapping,
        &error);
    QVERIFY2(affected != 0, error.c_str());
    // This global branch shares the pending prefix but is unreachable while
    // a matching window-local subtree owns resolution for this view.
    const MappingId unrelated = core.addMapping(
        actionMapping(U"dy", HostAction::FocusPanelRight),
        &error);
    QVERIFY2(unrelated != 0, error.c_str());

    const DispatchResult waiting =
        pressAscii(core, editor, QChar(u'd'));
    QCOMPARE(waiting.disposition, InputDisposition::Pending);
    QVERIFY(waiting.inputHintGeneration.has_value());
    const std::uint64_t generation =
        *waiting.inputHintGeneration;

    const MappingRevocation unrelatedRevocation =
        core.removeMappings(std::array{unrelated});
    QCOMPARE(unrelatedRevocation.removed, std::size_t{1});
    QVERIFY(!unrelatedRevocation.invalidatedInputGeneration);
    QVERIFY(core.pendingInputHint(generation).has_value());

    const MappingRevocation affectedRevocation =
        core.removeMappings(std::array{affected});
    QCOMPARE(affectedRevocation.removed, std::size_t{1});
    QCOMPARE(
        affectedRevocation.invalidatedInputGeneration,
        std::optional<std::uint64_t>{generation});
    QVERIFY(!core.pendingInputHint(generation).has_value());
    QVERIFY(core.mappingTimedOut(generation).events.empty());
    QCOMPARE(core.mode(), std::optional<Mode>{Mode::Normal});

    // The revoked `d` prefix must not be replayed into Vim's native delete
    // grammar. One fresh `d` therefore starts, rather than completes, `dd`.
    const DispatchResult fresh =
        pressAscii(core, editor, QChar(u'd'));
    QCOMPARE(fresh.disposition, InputDisposition::Pending);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>{Mode::OperatorPending});
    QCOMPARE(core.buffer(buffer)->text, std::u16string{u"text"});
}

void VkCoreTests::unicodeMappingUsesLogicalText()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    std::string error;
    QVERIFY(core.addMapping(
        actionMapping(U"λ", HostAction::FocusPanelRight),
        &error) != 0);
    const DispatchResult result = press(
        core,
        editor,
        0,
        Qt::NoModifier,
        QString::fromUtf8("λ"));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelRight));
}

void VkCoreTests::countsAndNormalCommandPrefixesAreNotMappings()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"zero\none\ntwo\nthree",
        {0, 0}) != 0);

    auto result = pressAscii(core, editor, QChar(u'2'));
    result = pressAscii(core, editor, QChar(u'j'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{2, 0}));

    result = pressAscii(core, editor, QChar(u'g'));
    QVERIFY(result.events.empty());
    result = pressAscii(core, editor, QChar(u'g'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 0}));

    const Qt::KeyboardModifiers control =
        vimControlModifier();
    result = press(
        core,
        editor,
        Qt::Key_W,
        control,
        {});
    QVERIFY(result.events.empty());
    result = pressAscii(core, editor, QChar(u'h'));
    QVERIFY(hasCommandRequest(
        result, WindowFocusLeftCommand));
}

void VkCoreTests::normalUndoRedoSupportsCountsAndBranches()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/undo.txt",
        u"one\ntwo\nthree\nfour",
        {0, 0});
    QVERIFY(buffer != 0);

    auto result = pressAscii(
        core, editor, QChar(u'd'));
    result = pressAscii(
        core, editor, QChar(u'd'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"two\nthree\nfour"));
    result = pressAscii(
        core, editor, QChar(u'd'));
    result = pressAscii(
        core, editor, QChar(u'd'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"three\nfour"));

    result = pressAscii(
        core, editor, QChar(u'2'));
    result = pressAscii(
        core, editor, QChar(u'u'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"one\ntwo\nthree\nfour"));
    QCOMPARE(
        std::count_if(
            result.events.cbegin(),
            result.events.cend(),
            [](const Event &event) {
                return event.type
                    == EventType::BufferEdited;
            }),
        2);
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type
                == EventType::CursorChanged;
        }));

    result = pressAscii(
        core, editor, QChar(u'2'));
    result = press(
        core,
        editor,
        Qt::Key_R,
        vimControlModifier(),
        QStringLiteral("r"));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"three\nfour"));
    QCOMPARE(
        std::count_if(
            result.events.cbegin(),
            result.events.cend(),
            [](const Event &event) {
                return event.type
                    == EventType::BufferEdited;
            }),
        2);

    // Undo the second deletion, then create a different child at the same
    // history node. Redo must follow the new preferred branch while the old
    // child remains retained by the branching undo tree.
    result = pressAscii(
        core, editor, QChar(u'u'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"two\nthree\nfour"));
    result = pressAscii(
        core, editor, QChar(u'd'));
    result = pressAscii(
        core, editor, QChar(u'l'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"wo\nthree\nfour"));
    result = pressAscii(
        core, editor, QChar(u'u'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"two\nthree\nfour"));
    result = press(
        core,
        editor,
        Qt::Key_R,
        vimControlModifier(),
        QStringLiteral("r"));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"wo\nthree\nfour"));
}

void VkCoreTests::insertSessionIsOneUndoBlock()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/insert-undo.txt",
        u"base",
        {0, 0});
    QVERIFY(buffer != 0);

    auto result = pressAscii(
        core, editor, QChar(u'i'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Insert));
    QVERIFY(core.applyExternalEdit(
        editor, 0, 0, u"A", {0, 1}));
    QVERIFY(core.applyExternalEdit(
        editor, 1, 0, u"B", {0, 2}));
    QVERIFY(core.applyExternalEdit(
        editor, 2, 0, u"C", {0, 3}));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"ABCbase"));

    result = press(
        core, editor, Qt::Key_Escape);
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    result = pressAscii(
        core, editor, QChar(u'u'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"base"));
    QCOMPARE(
        std::count_if(
            result.events.cbegin(),
            result.events.cend(),
            [](const Event &event) {
                return event.type
                    == EventType::BufferEdited;
            }),
        3);

    // A second undo is a no-op, proving the three host edits were one block.
    result = pressAscii(
        core, editor, QChar(u'u'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"base"));
    QVERIFY(std::none_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type
                == EventType::BufferEdited;
        }));

    result = press(
        core,
        editor,
        Qt::Key_R,
        vimControlModifier(),
        QStringLiteral("r"));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"ABCbase"));
}

void VkCoreTests::insertNavigationStartsANewUndoBlock()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/insert-navigation-undo.txt",
        u"xy",
        {0, 0});
    QVERIFY(buffer != 0);

    auto result =
        pressAscii(core, editor, QChar(u'i'));
    QVERIFY(core.applyExternalEdit(
        editor, 0, 0, u"A", {0, 1}));
    const std::array navigation{
        std::pair{Qt::Key_Left, InsertCommand::MoveLeft},
        std::pair{Qt::Key_Right, InsertCommand::MoveRight},
        std::pair{Qt::Key_Up, InsertCommand::MoveUp},
        std::pair{Qt::Key_Down, InsertCommand::MoveDown},
        std::pair{Qt::Key_Home, InsertCommand::MoveToStart},
        std::pair{Qt::Key_End, InsertCommand::MoveToEnd},
        std::pair{Qt::Key_PageUp, InsertCommand::PageUp},
        std::pair{Qt::Key_PageDown, InsertCommand::PageDown},
    };
    for (const auto &[key, command] : navigation) {
        const QKeyEvent event(
            QEvent::KeyPress,
            key,
            Qt::NoModifier);
        QVERIFY(core.shouldCapture(editor, event));
        result = core.dispatch(editor, editor, event);
        QCOMPARE(
            result.disposition,
            InputDisposition::Consumed);
        QVERIFY(hasInsertCommand(result, command));
    }

    // The host applies the requested cursor movement before reporting the
    // next concrete edit. That edit must start a fresh undo node.
    QVERIFY(core.setViewCursor(
        editor, Cursor{0, 2}));
    QVERIFY(core.applyExternalEdit(
        editor, 2, 0, u"B", {0, 3}));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"AxBy"));
    result = press(
        core, editor, Qt::Key_Escape);
    result =
        pressAscii(core, editor, QChar(u'u'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"Axy"));
    result =
        pressAscii(core, editor, QChar(u'u'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"xy"));
}

void VkCoreTests::controlNAndPMatchNativeLineMotions()
{
    VkCore core;
    const WindowId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/control-line-motions.txt",
        u"zero\none\ntwo\nthree\nfour",
        {1, 0}) != 0);

    auto result = press(
        core,
        editor,
        Qt::Key_N,
        vimControlModifier(),
        QStringLiteral("n"));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{2, 0}));

    result = pressAscii(core, editor, QChar(u'2'));
    result = press(
        core,
        editor,
        Qt::Key_P,
        vimControlModifier(),
        QStringLiteral("p"));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 0}));

    result = pressAscii(core, editor, QChar(u'v'));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Visual));
    result = pressAscii(core, editor, QChar(u'2'));
    result = press(
        core,
        editor,
        Qt::Key_N,
        vimControlModifier(),
        QStringLiteral("n"));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{2, 0}));
    const auto visual = core.window(editor);
    QVERIFY(visual.has_value());
    QCOMPARE(visual->visualAnchor, std::optional<Cursor>(Cursor{0, 0}));
    (void)press(core, editor, Qt::Key_Escape);

    const WindowId navigation =
        core.registerView(ViewKind::Navigation);
    result = press(
        core,
        navigation,
        Qt::Key_N,
        vimControlModifier(),
        QStringLiteral("n"));
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::HostAction
                && event.hostAction
                    == HostAction::NavigateDown;
        }));

    result = press(
        core,
        navigation,
        Qt::Key_P,
        vimControlModifier(),
        QStringLiteral("p"));
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::HostAction
                && event.hostAction
                    == HostAction::NavigateUp;
        }));

    VkCore operatorCore;
    const WindowId operatorWindow =
        operatorCore.registerWindow(WindowKind::Editor);
    const BufferId operatorBuffer = operatorCore.synchronizeBuffer(
        operatorWindow,
        "/vault/control-line-operator.txt",
        u"zero\none\ntwo",
        {0, 0});
    result = pressAscii(
        operatorCore, operatorWindow, QChar(u'd'));
    result = press(
        operatorCore,
        operatorWindow,
        Qt::Key_N,
        vimControlModifier(),
        QStringLiteral("n"));
    QCOMPARE(
        operatorCore.buffer(operatorBuffer)->text,
        std::u16string(u"two"));
    QCOMPARE(
        operatorCore.mode(),
        std::optional<Mode>(Mode::Normal));

}

void VkCoreTests::
    nativeColumnPercentageAndControlAliasesMatchNeovim()
{
    VkCore core;
    const WindowId editor =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/native-motion-gaps.txt",
        u"  aa\n  bb\n  cc\n  dd",
        {0, 3});
    QVERIFY(buffer != 0);

    auto result = press(
        core,
        editor,
        Qt::Key_H,
        vimControlModifier(),
        QStringLiteral("h"));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 2}));

    result = press(
        core,
        editor,
        Qt::Key_J,
        vimControlModifier(),
        QStringLiteral("j"));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{1, 2}));

    QVERIFY(core.setViewCursor(editor, Cursor{0, 3}));
    result = press(
        core,
        editor,
        Qt::Key_M,
        vimControlModifier(),
        QStringLiteral("m"));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{1, 2}));

    QVERIFY(core.setViewCursor(editor, Cursor{0, 3}));
    result = pressAscii(core, editor, QChar(u'_'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 2}));
    result = pressAscii(core, editor, QChar(u'2'));
    result = pressAscii(core, editor, QChar(u'_'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{1, 2}));

    result = pressAscii(core, editor, QChar(u'4'));
    result = pressAscii(core, editor, QChar(u'|'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{1, 3}));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"abcdefghij",
        {0, 0}));
    result = pressAscii(core, editor, QChar(u'9'));
    result = pressAscii(core, editor, QChar(u'0'));
    result = pressAscii(core, editor, QChar(u'g'));
    result = pressAscii(core, editor, QChar(u'M'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 9}));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"\tabcdefghi",
        {0, 0}));
    result = pressAscii(core, editor, QChar(u'g'));
    result = pressAscii(core, editor, QChar(u'M'));
    // Half of 17 display cells lands in the tab-adjacent 'a' cell, exactly
    // matching the Neovim --clean probe rather than a UTF-16 midpoint.
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 1}));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"  01\n  02\n  03\n  04\n  05\n  06\n  07\n  08\n  09\n  10",
        {0, 3}));
    result = pressAscii(core, editor, QChar(u'2'));
    result = pressAscii(core, editor, QChar(u'5'));
    result = pressAscii(core, editor, QChar(u'%'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{2, 2}));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"(nested)",
        {0, 0}));
    result = pressAscii(core, editor, QChar(u'%'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 7}));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"one\ntwo\nthree\nfour",
        {0, 0}));
    result = pressAscii(core, editor, QChar(u'd'));
    result = pressAscii(core, editor, QChar(u'5'));
    result = pressAscii(core, editor, QChar(u'0'));
    result = pressAscii(core, editor, QChar(u'%'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"three\nfour"));

    QVERIFY(core.replaceBufferText(
        buffer, u"abcdef", {0, 3}));
    result = pressAscii(core, editor, QChar(u'd'));
    result = pressAscii(core, editor, QChar(u'1'));
    result = pressAscii(core, editor, QChar(u'|'));
    QCOMPARE(core.buffer(buffer)->text, std::u16string(u"def"));

    QVERIFY(core.replaceBufferText(
        buffer, u"a\nb\nc", {0, 0}));
    result = pressAscii(core, editor, QChar(u'd'));
    result = press(
        core,
        editor,
        Qt::Key_J,
        vimControlModifier(),
        QStringLiteral("j"));
    QCOMPARE(core.buffer(buffer)->text, std::u16string(u"c"));

    QVERIFY(core.replaceBufferText(
        buffer, u"a\n  b\nc", {0, 0}));
    result = pressAscii(core, editor, QChar(u'd'));
    result = press(
        core,
        editor,
        Qt::Key_M,
        vimControlModifier(),
        QStringLiteral("m"));
    QCOMPARE(core.buffer(buffer)->text, std::u16string(u"c"));

    QVERIFY(core.replaceBufferText(
        buffer, u"abc", {0, 2}));
    result = pressAscii(core, editor, QChar(u'd'));
    result = press(
        core,
        editor,
        Qt::Key_H,
        vimControlModifier(),
        QStringLiteral("h"));
    QCOMPARE(core.buffer(buffer)->text, std::u16string(u"ac"));

    QVERIFY(core.replaceBufferText(
        buffer, u"a\n  b\nc", {0, 0}));
    result = pressAscii(core, editor, QChar(u'd'));
    result = pressAscii(core, editor, QChar(u'2'));
    result = pressAscii(core, editor, QChar(u'_'));
    QCOMPARE(core.buffer(buffer)->text, std::u16string(u"c"));
}

void VkCoreTests::
    displayPositionQueryUsesCoreSparseLayout()
{
    VkCore core;
    const WindowId editor =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/display-position.txt",
        u"\tA😀中e",
        {0, 0});
    QVERIFY(buffer != 0);
    QVERIFY(core.setBufferTabStop(buffer, 8));

    const std::array expected{
        std::pair{Cursor{0, 0}, std::size_t{0}},
        std::pair{Cursor{0, 1}, std::size_t{8}},
        std::pair{Cursor{0, 2}, std::size_t{9}},
        std::pair{Cursor{0, 4}, std::size_t{11}},
        std::pair{Cursor{0, 5}, std::size_t{13}},
        std::pair{Cursor{0, 6}, std::size_t{14}},
    };
    for (const auto &[cursor, column] : expected) {
        QCOMPARE(
            core.displayPosition(buffer, cursor),
            std::optional<DisplayPosition>(
                DisplayPosition{cursor, column}));
    }
    // UTF-16 column 3 splits the emoji surrogate pair.
    QVERIFY(!core.displayPosition(
        buffer, Cursor{0, 3}).has_value());
    QVERIFY(!core.displayPosition(
        buffer, Cursor{1, 0}).has_value());

    QCOMPARE(
        core.displayPositionForColumn(buffer, 0, 4),
        std::optional<DisplayPosition>(
            DisplayPosition{{0, 0}, 0}));
    QCOMPARE(
        core.displayPositionForColumn(buffer, 0, 10),
        std::optional<DisplayPosition>(
            DisplayPosition{{0, 2}, 9}));
    QCOMPARE(
        core.displayPositionForColumn(buffer, 0, 12),
        std::optional<DisplayPosition>(
            DisplayPosition{{0, 4}, 11}));
    QCOMPARE(
        core.displayPositionForColumn(buffer, 0, 20),
        std::optional<DisplayPosition>(
            DisplayPosition{{0, 5}, 13}));

    QCOMPARE(
        core.displayPositionForColumn(
            buffer,
            0,
            4,
            DisplayColumnMode::VisualBlock),
        std::optional<DisplayPosition>(
            DisplayPosition{{0, 0}, 4}));
    QCOMPARE(
        core.displayPositionForColumn(
            buffer,
            0,
            20,
            DisplayColumnMode::VisualBlock),
        std::optional<DisplayPosition>(
            DisplayPosition{{0, 6}, 14}));
    core.setVirtualEditMode(VirtualEditMode::Block);
    QCOMPARE(
        core.displayPositionForColumn(
            buffer,
            0,
            20,
            DisplayColumnMode::VisualBlock),
        std::optional<DisplayPosition>(
            DisplayPosition{{0, 6}, 20}));
    QVERIFY(!core.displayPositionForColumn(
        buffer, 1, 0).has_value());

    const auto stats = core.displayLayoutCacheStats(buffer);
    QVERIFY(stats.has_value());
    QCOMPARE(stats->cachedLines, std::size_t{1});
    QCOMPARE(stats->specialCells, std::size_t{3});
}

void VkCoreTests::
    displayRowMotionsUseARevisionCheckedRendererTransaction()
{
    VkCore core;
    const WindowId editor =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/wrapped-display-motion.txt",
        u"abcdefghij",
        {0, 0});
    QVERIFY(buffer != 0);

    auto result = pressAscii(core, editor, QChar(u'g'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, editor, QChar(u'j'));
    const Event *request = displayMotionRequest(result);
    QVERIFY(request != nullptr);
    QCOMPARE(request->displayMotionKind, DisplayMotionKind::RowDown);
    QCOMPARE(
        request->displayBufferRevision,
        core.buffer(buffer)->revision);
    QCOMPARE(request->displayPosition, (DisplayPosition{{0, 0}, 0}));
    QCOMPARE(request->count, std::size_t{1});
    QVERIFY(result.hostBarrierGeneration.has_value());
    QCOMPARE(core.window(editor)->cursor, (Cursor{0, 0}));

    const DisplayMotionRequestId firstRequest =
        request->displayMotionRequest;
    const auto firstDestination =
        core.displayPosition(buffer, Cursor{0, 4});
    QVERIFY(firstDestination.has_value());
    result = core.resolveDisplayMotion(
        firstRequest,
        DisplayMotionResolution{
            *firstDestination,
            std::size_t{2}});
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 4}));

    result = pressAscii(core, editor, QChar(u'g'));
    result = pressAscii(core, editor, QChar(u'k'));
    request = displayMotionRequest(result);
    QVERIFY(request != nullptr);
    QCOMPARE(request->displayMotionKind, DisplayMotionKind::RowUp);
    QCOMPARE(
        request->displayRowGoalColumn,
        std::optional<std::size_t>(2));
    result = core.resolveDisplayMotion(
        request->displayMotionRequest,
        DisplayMotionResolution{
            DisplayPosition{{0, 1}, 1},
            std::size_t{2}});
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 1}));

    result = pressAscii(core, editor, QChar(u'2'));
    result = pressAscii(core, editor, QChar(u'g'));
    result = pressAscii(core, editor, QChar(u'$'));
    request = displayMotionRequest(result);
    QVERIFY(request != nullptr);
    QCOMPARE(request->displayMotionKind, DisplayMotionKind::RowEnd);
    QCOMPARE(request->count, std::size_t{2});
    result = core.resolveDisplayMotion(
        request->displayMotionRequest,
        DisplayMotionResolution{DisplayPosition{{0, 8}, 8}, std::nullopt});
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 8}));

    const std::array rowLocalMotions{
        std::pair{QChar(u'0'), DisplayMotionKind::RowStart},
        std::pair{
            QChar(u'^'),
            DisplayMotionKind::RowFirstNonBlank},
        std::pair{QChar(u'm'), DisplayMotionKind::RowMiddle},
    };
    for (const auto &[key, kind] : rowLocalMotions) {
        result = pressAscii(core, editor, QChar(u'g'));
        result = pressAscii(core, editor, key);
        request = displayMotionRequest(result);
        QVERIFY(request != nullptr);
        QCOMPARE(request->displayMotionKind, kind);
        result = core.resolveDisplayMotion(
            request->displayMotionRequest,
            DisplayMotionResolution{
                DisplayPosition{{0, 8}, 8},
                std::nullopt});
        QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 8}));
    }

    result = pressAscii(core, editor, QChar(u'g'));
    result = pressAscii(core, editor, QChar(u'j'));
    request = displayMotionRequest(result);
    QVERIFY(request != nullptr);
    const DisplayMotionRequestId staleRequest =
        request->displayMotionRequest;
    QVERIFY(core.applyExternalEdit(
        editor, 10, 0, u"!", {0, 8}));
    result = core.resolveDisplayMotion(
        staleRequest,
        DisplayMotionResolution{
            DisplayPosition{{0, 9}, 9},
            std::size_t{2}});
    QVERIFY(std::ranges::any_of(
        result.events,
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));
    QCOMPARE(core.window(editor)->cursor, (Cursor{0, 8}));

    result = pressAscii(core, editor, QChar(u'g'));
    result = pressAscii(core, editor, QChar(u'0'));
    request = displayMotionRequest(result);
    QVERIFY(request != nullptr);
    QVERIFY(result.hostBarrierGeneration.has_value());
    result = core.acknowledgeHostBarrier(
        *result.hostBarrierGeneration,
        true,
        editor,
        editor);
    QVERIFY(std::ranges::any_of(
        result.events,
        [](const Event &event) {
            return event.type == EventType::InputError
                && event.message.find("resolveDisplayMotion")
                    != std::string::npos;
        }));

    VkCore operatorCore;
    const WindowId operatorWindow =
        operatorCore.registerWindow(WindowKind::Editor);
    const BufferId operatorBuffer =
        operatorCore.synchronizeBuffer(
            operatorWindow,
            "/vault/display-operator.txt",
            u"abcdef",
            {0, 0});
    result = pressAscii(operatorCore, operatorWindow, QChar(u'd'));
    result = pressAscii(operatorCore, operatorWindow, QChar(u'g'));
    result = pressAscii(operatorCore, operatorWindow, QChar(u'j'));
    request = displayMotionRequest(result);
    QVERIFY(request != nullptr);
    result = operatorCore.resolveDisplayMotion(
        request->displayMotionRequest,
        DisplayMotionResolution{DisplayPosition{{0, 3}, 3}, std::size_t{0}});
    QCOMPARE(
        operatorCore.buffer(operatorBuffer)->text,
        std::u16string(u"def"));
    QCOMPARE(
        operatorCore.mode(),
        std::optional<Mode>(Mode::Normal));

    VkCore mappedCore;
    const WindowId mappedWindow =
        mappedCore.registerWindow(WindowKind::Editor);
    QVERIFY(mappedCore.synchronizeBuffer(
        mappedWindow,
        "/vault/display-mapping.txt",
        u"abcdef",
        {0, 0}) != 0);
    MappingDefinition mappedMotion = keyMapping(
        U"Q",
        U"gjl",
        RemapPolicy::NoRemap);
    QVERIFY(mappedCore.addMapping(mappedMotion) != 0);
    result = pressAscii(
        mappedCore, mappedWindow, QChar(u'Q'));
    request = displayMotionRequest(result);
    QVERIFY(request != nullptr);
    QVERIFY(result.hostBarrierGeneration.has_value());
    result = mappedCore.resolveDisplayMotion(
        request->displayMotionRequest,
        DisplayMotionResolution{
            DisplayPosition{{0, 3}, 3},
            std::size_t{0}});
    // The queued l executes only after the renderer commits gj.
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 4}));
}

void VkCoreTests::halfPageScrollIsWindowLocal()
{
    VkCore core;
    const WindowId first =
        core.registerView(ViewKind::Editor);
    const WindowId second =
        core.registerView(ViewKind::Editor);
    std::u16string text;
    for (int line = 0; line < 20; ++line) {
        if (!text.empty()) {
            text.push_back(u'\n');
        }
        text += u"x";
    }
    const BufferId buffer = core.synchronizeBuffer(
        first,
        "/vault/scroll.txt",
        text,
        {0, 0});
    QCOMPARE(
        core.synchronizeBuffer(
            second,
            "/vault/scroll.txt",
            text,
            {10, 0}),
        buffer);
    QVERIFY(core.setWindowViewport(
        first, 0, 0, 6));
    QVERIFY(core.setWindowViewport(
        second, 5, 0, 8));

    auto result = press(
        core,
        first,
        Qt::Key_D,
        vimControlModifier(),
        QStringLiteral("d"));
    auto firstState = core.window(first);
    auto secondState = core.window(second);
    QVERIFY(firstState.has_value());
    QVERIFY(secondState.has_value());
    QCOMPARE(firstState->topline, std::size_t{3});
    QCOMPARE(firstState->cursor, (Cursor{3, 0}));
    QCOMPARE(secondState->topline, std::size_t{5});
    QCOMPARE(secondState->cursor, (Cursor{10, 0}));
    QVERIFY(std::any_of(
        result.events.cbegin(),
        result.events.cend(),
        [first](const Event &event) {
            return event.type
                    == EventType::ViewportChanged
                && event.view == first;
        }));

    result = pressAscii(
        core, first, QChar(u'2'));
    result = press(
        core,
        first,
        Qt::Key_D,
        vimControlModifier(),
        QStringLiteral("d"));
    firstState = core.window(first);
    QCOMPARE(firstState->topline, std::size_t{5});
    QCOMPARE(firstState->cursor, (Cursor{5, 0}));
    QCOMPARE(firstState->scrollRows, std::size_t{2});

    // Host layout/scroll synchronization does not reset the window-local
    // option set by an explicit count.
    QVERIFY(core.setWindowViewport(
        first, 5, 0, 6));
    result = press(
        core,
        first,
        Qt::Key_U,
        vimControlModifier(),
        QStringLiteral("u"));
    firstState = core.window(first);
    QCOMPARE(firstState->topline, std::size_t{3});
    QCOMPARE(firstState->cursor, (Cursor{3, 0}));

    // Explicit zero restores the automatic half-window distance.
    QVERIFY(core.setWindowViewport(
        first, 3, 0, 6, std::size_t{0}));
    result = press(
        core,
        first,
        Qt::Key_D,
        vimControlModifier(),
        QStringLiteral("d"));
    firstState = core.window(first);
    QCOMPARE(firstState->topline, std::size_t{6});
    QCOMPARE(firstState->cursor, (Cursor{6, 0}));
    QCOMPARE(firstState->scrollRows, std::size_t{0});

    result = press(
        core,
        second,
        Qt::Key_U,
        vimControlModifier(),
        QStringLiteral("u"));
    firstState = core.window(first);
    secondState = core.window(second);
    QCOMPARE(firstState->topline, std::size_t{6});
    QCOMPARE(firstState->cursor, (Cursor{6, 0}));
    QCOMPARE(secondState->topline, std::size_t{1});
    QCOMPARE(secondState->cursor, (Cursor{6, 0}));

    // A structured renderer may measure half-page motion in visual pixels.
    // Core keeps the grammar/count but must not first mutate logical rows;
    // doing both produces a viewport jump followed by a cursor reveal.
    QVERIFY(core.setWindowViewportMotionAuthority(
        first, ViewportMotionAuthority::HostVisual));
    const WindowSnapshot visualBefore = *core.window(first);
    result = press(
        core,
        first,
        Qt::Key_D,
        vimControlModifier(),
        QStringLiteral("d"));
    QVERIFY(hasHostAction(
        result, HostAction::NavigateHalfPageDown));
    const WindowSnapshot visualAfter = *core.window(first);
    QCOMPARE(visualAfter.topline, visualBefore.topline);
    QCOMPARE(visualAfter.cursor, visualBefore.cursor);
    QCOMPARE(
        visualAfter.viewportMotionAuthority,
        ViewportMotionAuthority::HostVisual);

    result = press(
        core,
        first,
        Qt::Key_E,
        vimControlModifier(),
        QStringLiteral("e"));
    QVERIFY(hasHostAction(
        result, HostAction::ScrollViewportLineDown));
    result = press(
        core,
        first,
        Qt::Key_Y,
        vimControlModifier(),
        QStringLiteral("y"));
    QVERIFY(hasHostAction(
        result, HostAction::ScrollViewportLineUp));

    const auto viewportCommand = [&core, first](
                                     const QChar key,
                                     const HostAction expected,
                                     const std::size_t count = 1) {
        auto command = pressAscii(core, first, QChar(u'z'));
        QCOMPARE(command.disposition, InputDisposition::Pending);
        command = pressAscii(core, first, key);
        const auto action = std::ranges::find_if(
            command.events,
            [expected](const Event &event) {
                return event.type == EventType::HostAction
                    && event.hostAction == expected;
            });
        QVERIFY(action != command.events.cend());
        QCOMPARE(action->count, count);
    };
    viewportCommand(
        QChar(u't'), HostAction::PositionViewportCursorTop);
    viewportCommand(
        QChar(u'z'), HostAction::PositionViewportCursorCenter);
    viewportCommand(
        QChar(u'b'), HostAction::PositionViewportCursorBottom);
    viewportCommand(
        QChar(u'h'), HostAction::ScrollViewportLeft);
    viewportCommand(
        QChar(u'l'), HostAction::ScrollViewportRight);
    viewportCommand(
        QChar(u'H'), HostAction::ScrollViewportHalfLeft);
    viewportCommand(
        QChar(u'L'), HostAction::ScrollViewportHalfRight);
    viewportCommand(
        QChar(u's'), HostAction::PositionViewportCursorStart);
    viewportCommand(
        QChar(u'e'), HostAction::PositionViewportCursorEnd);

    result = pressAscii(core, first, QChar(u'3'));
    result = pressAscii(core, first, QChar(u'z'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressAscii(core, first, QChar(u'l'));
    const auto counted = std::ranges::find_if(
        result.events,
        [](const Event &event) {
            return event.type == EventType::HostAction
                && event.hostAction == HostAction::ScrollViewportRight;
        });
    QVERIFY(counted != result.events.cend());
    QCOMPARE(counted->count, std::size_t{3});
    QVERIFY(counted->countWasExplicit);
}

void VkCoreTests::semanticViewJumpParticipatesInJumpHistory()
{
    VkCore core;
    const ViewId reader = core.registerView(ViewKind::Surface);
    QVERIFY(core.synchronizeBuffer(
        reader,
        "/vault/paper.pdf",
        u"reference\nbody\ntarget",
        {0, 0}) != 0);
    QVERIFY(core.jumpViewCursor(reader, Cursor{2, 0}));
    QCOMPARE(core.window(reader)->cursor, (Cursor{2, 0}));

    auto result = press(
        core,
        reader,
        Qt::Key_O,
        vimControlModifier(),
        QStringLiteral("o"));
    QCOMPARE(core.window(reader)->cursor, (Cursor{0, 0}));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 0}));
    result = press(
        core,
        reader,
        Qt::Key_I,
        vimControlModifier(),
        QStringLiteral("i"));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{2, 0}));
}

void VkCoreTests::
    characterSearchMotionsRepeatAndDriveOperators()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/character-search.txt",
        u"0a1a2a3",
        {0, 0});
    QVERIFY(buffer != 0);

    auto result =
        pressAscii(core, editor, QChar(u'2'));
    result =
        pressAscii(core, editor, QChar(u'f'));
    QVERIFY(!lastCursor(result).has_value());
    result =
        pressAscii(core, editor, QChar(u'a'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 3}));
    result =
        pressAscii(core, editor, QChar(u';'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 5}));
    result =
        pressAscii(core, editor, QChar(u','));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 3}));

    // Counted searches commit atomically when every occurrence exists.
    result =
        pressAscii(core, editor, QChar(u'2'));
    result =
        pressAscii(core, editor, QChar(u','));
    QVERIFY(!lastCursor(result).has_value());
    QCOMPARE(
        core.window(editor)->cursor,
        (Cursor{0, 3}));

    QVERIFY(core.setViewCursor(
        editor, Cursor{0, 0}));
    result =
        pressAscii(core, editor, QChar(u't'));
    result =
        pressAscii(core, editor, QChar(u'a'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 0}));
    result =
        pressAscii(core, editor, QChar(u';'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 2}));
    QVERIFY(core.setViewCursor(
        editor, Cursor{0, 7}));
    result =
        pressAscii(core, editor, QChar(u'F'));
    result =
        pressAscii(core, editor, QChar(u'a'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 5}));
    result =
        pressAscii(core, editor, QChar(u'T'));
    result =
        pressAscii(core, editor, QChar(u'a'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 4}));

    QVERIFY(core.replaceBufferText(
        buffer, u"x😀y😀z", {0, 0}));
    result =
        pressAscii(core, editor, QChar(u'2'));
    result =
        pressAscii(core, editor, QChar(u'f'));
    result = press(
        core,
        editor,
        0,
        Qt::NoModifier,
        QString::fromUtf8("😀"));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 4}));

    QVERIFY(core.replaceBufferText(
        buffer, u"abcdefghi", {0, 0}));
    result =
        pressAscii(core, editor, QChar(u'd'));
    result =
        pressAscii(core, editor, QChar(u'f'));
    result =
        pressAscii(core, editor, QChar(u'e'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"fghi"));
    (void)core.undo(editor);

    // Repeats are motions in Operator-pending too; the range is still
    // resolved by the common characterwise operator pipeline.
    QVERIFY(core.setViewCursor(
        editor, Cursor{0, 0}));
    result =
        pressAscii(core, editor, QChar(u'f'));
    result =
        pressAscii(core, editor, QChar(u'e'));
    QVERIFY(core.setViewCursor(
        editor, Cursor{0, 0}));
    result =
        pressAscii(core, editor, QChar(u'd'));
    result =
        pressAscii(core, editor, QChar(u';'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"fghi"));
    (void)core.undo(editor);

    result =
        pressAscii(core, editor, QChar(u'd'));
    result =
        pressAscii(core, editor, QChar(u't'));
    result =
        pressAscii(core, editor, QChar(u'e'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"efghi"));
    (void)core.undo(editor);

    QVERIFY(core.setViewCursor(
        editor, Cursor{0, 4}));
    result =
        pressAscii(core, editor, QChar(u'd'));
    result =
        pressAscii(core, editor, QChar(u'F'));
    result =
        pressAscii(core, editor, QChar(u'b'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"aefghi"));
    (void)core.undo(editor);

    QVERIFY(core.setViewCursor(
        editor, Cursor{0, 4}));
    result =
        pressAscii(core, editor, QChar(u'd'));
    result =
        pressAscii(core, editor, QChar(u'T'));
    result =
        pressAscii(core, editor, QChar(u'b'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"abefghi"));
    (void)core.undo(editor);

    QVERIFY(core.setViewCursor(
        editor, Cursor{0, 0}));
    result =
        pressAscii(core, editor, QChar(u'd'));
    result =
        pressAscii(core, editor, QChar(u'f'));
    result =
        pressAscii(core, editor, QChar(u'z'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"abcdefghi"));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Normal));
    QCOMPARE(
        core.window(editor)->cursor,
        (Cursor{0, 0}));

    QVERIFY(core.setViewCursor(
        editor, Cursor{0, 0}));
    result =
        pressAscii(core, editor, QChar(u'v'));
    result =
        pressAscii(core, editor, QChar(u'f'));
    result =
        pressAscii(core, editor, QChar(u'e'));
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Visual));
    QCOMPARE(
        core.window(editor)->visualAnchor,
        std::optional<Cursor>(Cursor{0, 0}));
    QCOMPARE(
        core.window(editor)->cursor,
        (Cursor{0, 4}));
}

void VkCoreTests::matchingPairsAreNestedAndUtf16Safe()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const std::u16string nested =
        u"😀 xx (a[b{c}d]e) tail";
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/pairs.txt",
        nested,
        {0, 0});
    QVERIFY(buffer != 0);
    const std::size_t opening =
        nested.find(u'(');
    const std::size_t closing =
        nested.rfind(u')');

    auto result =
        pressAscii(core, editor, QChar(u'%'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(
            Cursor{0, closing}));
    result =
        pressAscii(core, editor, QChar(u'%'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(
            Cursor{0, opening}));

    const std::size_t innerOpen =
        nested.find(u'{');
    const std::size_t innerClose =
        nested.find(u'}');
    QVERIFY(core.setViewCursor(
        editor, Cursor{0, innerOpen}));
    result =
        pressAscii(core, editor, QChar(u'%'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(
            Cursor{0, innerClose}));

    QVERIFY(core.replaceBufferText(
        buffer, u"😀 (", {0, 0}));
    result =
        pressAscii(core, editor, QChar(u'%'));
    QVERIFY(!lastCursor(result).has_value());
    QCOMPARE(
        core.window(editor)->cursor,
        (Cursor{0, 0}));

    // Inclusive '%' includes both delimiters when travelling backward.
    QVERIFY(core.replaceBufferText(
        buffer, u"x(a[b]c)y", {0, 7}));
    result =
        pressAscii(core, editor, QChar(u'd'));
    result =
        pressAscii(core, editor, QChar(u'%'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"xy"));
}

void VkCoreTests::windowMotionsUseTheWindowViewport()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/window-motions.txt",
        u"0\n1\n2\n3\n4\n5\n6\n7\n8\n9",
        {0, 0});
    QVERIFY(buffer != 0);
    QVERIFY(core.setWindowViewport(
        editor, 3, 0, 4, std::nullopt));

    auto result =
        pressAscii(core, editor, QChar(u'H'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{3, 0}));
    result =
        pressAscii(core, editor, QChar(u'2'));
    result =
        pressAscii(core, editor, QChar(u'H'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{4, 0}));
    result =
        pressAscii(core, editor, QChar(u'M'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{4, 0}));
    result =
        pressAscii(core, editor, QChar(u'L'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{6, 0}));
    result =
        pressAscii(core, editor, QChar(u'2'));
    result =
        pressAscii(core, editor, QChar(u'L'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{5, 0}));

    result =
        pressAscii(core, editor, QChar(u'd'));
    result =
        pressAscii(core, editor, QChar(u'H'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(
            u"0\n1\n2\n6\n7\n8\n9"));
}

void VkCoreTests::normalCursorNeverSplitsSurrogatePairs()
{
    VkCore core;
    const WindowId editor =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        editor,
        "/vault/surrogate.txt",
        u"A😀B",
        {0, 3});
    QVERIFY(buffer != 0);

    auto result = pressAscii(
        core, editor, QChar(u'h'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 1}));
    result = pressAscii(
        core, editor, QChar(u'h'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 0}));
    result = pressAscii(
        core, editor, QChar(u'l'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 1}));
    result = pressAscii(
        core, editor, QChar(u'l'));
    QCOMPARE(
        lastCursor(result),
        std::optional<Cursor>(Cursor{0, 3}));

    // Host cursor updates are clamped to the start of a UTF-16 scalar too.
    QVERIFY(core.setViewCursor(editor, {0, 2}));
    QCOMPARE(
        core.window(editor)->cursor,
        (Cursor{0, 1}));

    result = pressAscii(
        core, editor, QChar(u'd'));
    result = pressAscii(
        core, editor, QChar(u'l'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"AB"));
    result = pressAscii(
        core, editor, QChar(u'u'));
    QCOMPARE(
        core.buffer(buffer)->text,
        std::u16string(u"A😀B"));
    QCOMPARE(
        core.window(editor)->cursor,
        (Cursor{0, 1}));
}

void VkCoreTests::buffersAndViewsKeepIndependentCursors()
{
    VkCore core;
    const ViewId firstView =
        core.registerView(ViewKind::Editor);
    const ViewId secondView =
        core.registerView(ViewKind::Editor);
    const BufferId first = core.synchronizeBuffer(
        firstView,
        "/vault/first.txt",
        u"one\ntwo",
        {1, 1});
    QVERIFY(core.attachBuffer(
        secondView, first, {0, 0}));
    QCOMPARE(
        core.window(secondView)->buffer,
        first);
    QCOMPARE(core.bufferCount(), std::size_t{1});

    auto result =
        pressAscii(core, firstView, QChar(u'k'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 1}));
    result = pressAscii(core, secondView, QChar(u'l'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 1}));
    result = pressAscii(core, firstView, QChar(u'l'));
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 2}));

    // Reattaching a second window changes only that window's cursor. It must
    // neither import renderer text nor reset the buffer's undo graph.
    QVERIFY(core.applyExternalEdit(
        firstView, 0, 0, u"X", {0, 1}));
    QCOMPARE(
        core.buffer(first)->text,
        std::u16string(u"Xone\ntwo"));
    QVERIFY(core.attachBuffer(
        secondView, first, {1, 2}));
    QCOMPARE(
        core.window(firstView)->cursor,
        (Cursor{0, 1}));
    QCOMPARE(
        core.window(secondView)->cursor,
        (Cursor{1, 2}));
    (void)core.undo(firstView);
    QCOMPARE(
        core.buffer(first)->text,
        std::u16string(u"one\ntwo"));
    QCOMPARE(
        core.window(firstView)->cursor,
        (Cursor{0, 2}));
    QCOMPARE(
        core.window(secondView)->cursor,
        (Cursor{0, 1}));

    QVERIFY(core.setActiveView(firstView));
    QVERIFY(core.replaceBufferText(
        first, u"one\ntwo\nthree", {1, 0}));
    result = pressAscii(core, secondView, QChar(u'l'));
    // Replacing shared text updates the active view's supplied cursor but
    // clamps, rather than overwrites, every other view's cursor.
    QCOMPARE(lastCursor(result), std::optional<Cursor>(Cursor{0, 2}));
}

void VkCoreTests::bufferActivationRequiresHostCommit()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId first = core.synchronizeBuffer(
        editor,
        "/vault/first.txt",
        u"first",
        {0, 0});
    const BufferId second = core.synchronizeBuffer(
        editor,
        "/vault/second.txt",
        u"second",
        {0, 0});
    QVERIFY(first != 0);
    QVERIFY(second != 0);
    QCOMPARE(
        core.activeBuffer(editor)->id,
        second);

    auto result =
        pressAscii(core, editor, QChar(u'['));
    result = pressAscii(core, editor, QChar(u'b'));
    QCOMPARE(
        lastActivationRequest(result),
        std::optional<BufferId>(first));
    // A request alone never moves the core. If the host rejects save/load,
    // subsequent commands must continue to operate on the visible buffer.
    QCOMPARE(
        core.activeBuffer(editor)->id,
        second);
    result = pressAscii(core, editor, QChar(u'd'));
    result = pressAscii(core, editor, QChar(u'd'));
    QCOMPARE(
        core.buffer(second)->text,
        std::u16string{});
    QCOMPARE(
        core.buffer(first)->text,
        std::u16string(u"first"));

    // synchronizeBuffer is the host's successful activation commit.
    QCOMPARE(
        core.synchronizeBuffer(
            editor,
            "/vault/first.txt",
            u"first",
            {0, 0}),
        first);
    QCOMPARE(
        core.activeBuffer(editor)->id,
        first);
}

void VkCoreTests::
    hostBarrierRejectsOrCommitsRemainingBufferRhs()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId first = core.synchronizeBuffer(
        editor,
        "/vault/first.txt",
        u"first",
        {0, 0});
    const BufferId second = core.synchronizeBuffer(
        editor,
        "/vault/second.txt",
        u"second",
        {0, 0});
    QCOMPARE(
        core.synchronizeBuffer(
            editor,
            "/vault/first.txt",
            u"first",
            {0, 0}),
        first);

    std::string error;
    QVERIFY2(
        core.addMapping(
            keyMapping(U"x", U"]bdd"),
            &error)
            != 0,
        error.c_str());

    DispatchResult result =
        pressAscii(core, editor, QChar(u'x'));
    QCOMPARE(
        lastActivationRequest(result),
        std::optional<BufferId>(second));
    QVERIFY(result.hostBarrierGeneration);
    const std::uint64_t rejectedGeneration =
        *result.hostBarrierGeneration;

    // Physical input cannot overtake the suspended mapping transaction.
    const DispatchResult blocked =
        pressAscii(core, editor, QChar(u'd'));
    QCOMPARE(
        blocked.disposition,
        InputDisposition::Consumed);
    QVERIFY(std::any_of(
        blocked.events.cbegin(),
        blocked.events.cend(),
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));
    QCOMPARE(
        core.buffer(first)->text,
        std::u16string(u"first"));

    result = core.acknowledgeHostBarrier(
        rejectedGeneration,
        false,
        editor,
        editor);
    QCOMPARE(
        result.disposition,
        InputDisposition::Consumed);
    QVERIFY(!result.hostBarrierGeneration);
    QCOMPARE(
        core.buffer(first)->text,
        std::u16string(u"first"));
    QCOMPARE(
        core.buffer(second)->text,
        std::u16string(u"second"));

    result = pressAscii(core, editor, QChar(u'x'));
    QVERIFY(result.hostBarrierGeneration);
    const std::uint64_t committedGeneration =
        *result.hostBarrierGeneration;
    QCOMPARE(
        core.synchronizeBuffer(
            editor,
            "/vault/second.txt",
            u"second",
            {0, 0}),
        second);
    result = core.acknowledgeHostBarrier(
        committedGeneration,
        true,
        editor,
        editor);
    QVERIFY(!result.hostBarrierGeneration);
    QCOMPARE(
        core.buffer(second)->text,
        std::u16string{});
    QCOMPARE(
        core.buffer(first)->text,
        std::u16string(u"first"));
}

void VkCoreTests::
    hostBarriersSequenceAcrossBufferActivations()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId first = core.synchronizeBuffer(
        editor, "/vault/a.txt", u"a", {0, 0});
    const BufferId second = core.synchronizeBuffer(
        editor, "/vault/b.txt", u"b", {0, 0});
    const BufferId third = core.synchronizeBuffer(
        editor, "/vault/c.txt", u"c", {0, 0});
    QCOMPARE(
        core.synchronizeBuffer(
            editor, "/vault/a.txt", u"a", {0, 0}),
        first);

    std::string error;
    QVERIFY2(
        core.addMapping(
            keyMapping(U"x", U"]b]b"),
            &error)
            != 0,
        error.c_str());

    DispatchResult result =
        pressAscii(core, editor, QChar(u'x'));
    QCOMPARE(
        lastActivationRequest(result),
        std::optional<BufferId>(second));
    QVERIFY(result.hostBarrierGeneration);
    const std::uint64_t firstGeneration =
        *result.hostBarrierGeneration;

    // A stale acknowledgement must neither resume nor discard the live tail.
    result = core.acknowledgeHostBarrier(
        firstGeneration + 100,
        true,
        editor,
        editor);
    QCOMPARE(
        result.disposition,
        InputDisposition::PassThrough);
    QCOMPARE(
        core.synchronizeBuffer(
            editor, "/vault/b.txt", u"b", {0, 0}),
        second);

    result = core.acknowledgeHostBarrier(
        firstGeneration,
        true,
        editor,
        editor);
    QCOMPARE(
        lastActivationRequest(result),
        std::optional<BufferId>(third));
    // The final request has no same-transaction continuation, so it can be
    // committed synchronously without another core round trip.
    QVERIFY(!result.hostBarrierGeneration);
    QCOMPARE(
        core.synchronizeBuffer(
            editor, "/vault/c.txt", u"c", {0, 0}),
        third);

    // Duplicating the completed acknowledgement is a strict no-op.
    result = core.acknowledgeHostBarrier(
        firstGeneration,
        false,
        editor,
        editor);
    QCOMPARE(
        result.disposition,
        InputDisposition::PassThrough);
    QVERIFY(!result.hostBarrierGeneration);
    QCOMPARE(core.activeBuffer(editor)->id, third);

    // A later host request with its own continuation yields a fresh barrier,
    // and a duplicate acknowledgement of the previous generation cannot
    // disturb it.
    QVERIFY2(
        core.addMapping(
            keyMapping(U"y", U"]b]bdd"),
            &error)
            != 0,
        error.c_str());
    result = pressAscii(core, editor, QChar(u'y'));
    QCOMPARE(
        lastActivationRequest(result),
        std::optional<BufferId>(first));
    QVERIFY(result.hostBarrierGeneration);
    const std::uint64_t chainedFirstGeneration =
        *result.hostBarrierGeneration;
    QCOMPARE(
        core.synchronizeBuffer(
            editor, "/vault/a.txt", u"a", {0, 0}),
        first);
    result = core.acknowledgeHostBarrier(
        chainedFirstGeneration,
        true,
        editor,
        editor);
    QCOMPARE(
        lastActivationRequest(result),
        std::optional<BufferId>(second));
    QVERIFY(result.hostBarrierGeneration);
    const std::uint64_t chainedSecondGeneration =
        *result.hostBarrierGeneration;

    result = core.acknowledgeHostBarrier(
        chainedFirstGeneration,
        false,
        editor,
        editor);
    QCOMPARE(
        result.disposition,
        InputDisposition::PassThrough);
    QCOMPARE(
        core.synchronizeBuffer(
            editor, "/vault/b.txt", u"b", {0, 0}),
        second);
    result = core.acknowledgeHostBarrier(
        chainedSecondGeneration,
        true,
        editor,
        editor);
    QVERIFY(!result.hostBarrierGeneration);
    QCOMPARE(
        core.buffer(second)->text,
        std::u16string{});
}

void VkCoreTests::
    hostBarrierResumesInResultingNavigationContext()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const ViewId navigation =
        core.registerView(ViewKind::Navigation);
    const BufferId document = core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"must remain",
        {0, 0});

    std::string error;
    QVERIFY2(
        core.addMapping(
            keyMapping(U"x", U"<C-w>hdd"),
            &error)
            != 0,
        error.c_str());

    DispatchResult result =
        pressAscii(core, editor, QChar(u'x'));
    QVERIFY(hasCommandRequest(
        result, WindowFocusLeftCommand));
    QVERIFY(result.hostBarrierGeneration);
    const std::uint64_t generation =
        *result.hostBarrierGeneration;

    // Host focus plumbing is allowed to run before the explicit commit
    // without cancelling or prematurely draining the suspended "dd".
    result = core.transitionInputContext(
        navigation, 77);
    QCOMPARE(
        result.disposition,
        InputDisposition::Consumed);
    QVERIFY(core.setActiveView(navigation));
    QCOMPARE(
        core.buffer(document)->text,
        std::u16string(u"must remain"));

    result = core.acknowledgeHostBarrier(
        generation,
        true,
        navigation,
        77);
    QVERIFY(!result.hostBarrierGeneration);
    QCOMPARE(core.activeView(), navigation);
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Normal));
    QCOMPARE(
        core.buffer(document)->text,
        std::u16string(u"must remain"));
    QVERIFY(std::none_of(
        result.events.cbegin(),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::BufferEdited;
        }));
}

void VkCoreTests::
    dispatchCannotOvertakeBarrierCreatedByPrefixDrain()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        editor,
        "/vault/note.txt",
        u"keep",
        {0, 0}) != 0);

    std::string error;
    QVERIFY2(
        core.addMapping(
            keyMapping(U"a", U"yz"),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"ab", HostAction::FocusPanelRight),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        core.addMapping(
            actionMapping(
                U"y", HostAction::FocusPanelLeft),
            &error)
            != 0,
        error.c_str());

    DispatchResult result =
        pressAscii(core, editor, QChar(u'a'));
    QCOMPARE(
        result.disposition,
        InputDisposition::Pending);

    // The mismatching "d" first resolves the old short mapping. That
    // resolution reaches a host action with "z" still queued, so "d" must
    // not be appended behind the barrier.
    result = pressAscii(core, editor, QChar(u'd'));
    QVERIFY(hasHostAction(
        result, HostAction::FocusPanelLeft));
    QVERIFY(result.hostBarrierGeneration);
    const std::uint64_t generation =
        *result.hostBarrierGeneration;
    result = core.acknowledgeHostBarrier(
        generation,
        true,
        editor,
        editor);
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Normal));
    QCOMPARE(
        core.activeBuffer(editor)->text,
        std::u16string(u"keep"));
}

void VkCoreTests::
    bufferPathsRebindAndRemoveWithoutGhostEntries()
{
    VkCore core;
    const ViewId editor =
        core.registerView(ViewKind::Editor);
    const BufferId first = core.synchronizeBuffer(
        editor,
        "/vault/Old/First.txt",
        u"first",
        {0, 0});
    const BufferId second = core.synchronizeBuffer(
        editor,
        "/vault/Old/Nested/Second.txt",
        u"second",
        {0, 0});
    const BufferId other = core.synchronizeBuffer(
        editor,
        "/vault/Other.txt",
        u"other",
        {0, 0});
    const BufferId collision = core.synchronizeBuffer(
        editor,
        "/vault/New/First.txt",
        u"stale target",
        {0, 0});
    QVERIFY(first != 0);
    QVERIFY(second != 0);
    QVERIFY(other != 0);
    QVERIFY(collision != 0);
    QCOMPARE(core.bufferCount(), std::size_t{4});
    QCOMPARE(
        core.activeBuffer(editor)->id,
        collision);

    std::size_t rebound = 99;
    QVERIFY(!core.rebindBuffersUnderPath(
        "/vault/Old",
        "/vault/New",
        &rebound));
    QCOMPARE(rebound, std::size_t{0});
    QCOMPARE(
        core.bufferForPath("/vault/Old/First.txt"),
        std::optional<BufferId>(first));
    QCOMPARE(
        core.bufferForPath(
            "/vault/Old/Nested/Second.txt"),
        std::optional<BufferId>(second));

    // Even an explicitly replaceable collision is not stale while a view is
    // attached to it. The operation is rejected atomically: no path owner,
    // active binding, or source buffer changes.
    QVERIFY(!core.rebindBufferPath(
        first,
        "/vault/New/First.txt",
        true));
    QCOMPARE(
        core.activeBuffer(editor)->id,
        collision);
    QVERIFY(core.buffer(collision));
    QCOMPARE(
        core.bufferForPath("/vault/Old/First.txt"),
        std::optional<BufferId>(first));
    QCOMPARE(
        core.bufferForPath("/vault/New/First.txt"),
        std::optional<BufferId>(collision));
    QVERIFY(!core.rebindBuffersUnderPath(
        "/vault/Old",
        "/vault/New",
        &rebound,
        true));
    QCOMPARE(rebound, std::size_t{0});
    QCOMPARE(
        core.activeBuffer(editor)->id,
        collision);
    QCOMPARE(core.bufferCount(), std::size_t{4});

    // Once the destination is detached it is stale cache state and the host
    // may replace it after a successful filesystem transaction.
    QCOMPARE(
        core.synchronizeBuffer(
            editor,
            "/vault/Other.txt",
            u"other",
            {0, 0}),
        other);
    QVERIFY(core.rebindBuffersUnderPath(
        "/vault/Old",
        "/vault/New",
        &rebound,
        true));
    QCOMPARE(rebound, std::size_t{2});
    QVERIFY(!core.buffer(collision));
    QVERIFY(!core.bufferForPath(
        "/vault/Old/First.txt"));
    QCOMPARE(
        core.bufferForPath("/vault/New/First.txt"),
        std::optional<BufferId>(first));
    QCOMPARE(
        core.bufferForPath(
            "/vault/New/Nested/Second.txt"),
        std::optional<BufferId>(second));
    QCOMPARE(
        core.buffer(first)->path,
        std::string("/vault/New/First.txt"));

    QVERIFY(core.rebindBufferPath(
        other, "/vault/Other Renamed.txt"));
    QCOMPARE(
        core.bufferForPath("/vault/Other Renamed.txt"),
        std::optional<BufferId>(other));
    QVERIFY(!core.bufferForPath("/vault/Other.txt"));

    QCOMPARE(
        core.synchronizeBuffer(
            editor,
            "/vault/New/Nested/Second.txt",
            u"second",
            {0, 1}),
        second);
    QCOMPARE(
        core.removeBuffersUnderPath("/vault/New"),
        std::size_t{2});
    QVERIFY(!core.buffer(first));
    QVERIFY(!core.buffer(second));
    QCOMPARE(core.bufferCount(), std::size_t{1});
    QVERIFY(!core.activeBuffer(editor));

    auto result =
        pressAscii(core, editor, QChar(u']'));
    result = pressAscii(core, editor, QChar(u'b'));
    QCOMPARE(
        lastActivationRequest(result),
        std::optional<BufferId>(other));
    QVERIFY(!lastActivationRequest(result)
                 || (*lastActivationRequest(result) != first
                     && *lastActivationRequest(result) != second));

    QVERIFY(core.detachViewBuffer(editor));
    QVERIFY(!core.activeBuffer(editor));
    QVERIFY(core.removeBuffer(other));
    QCOMPARE(core.bufferCount(), std::size_t{0});
    result = pressAscii(core, editor, QChar(u'['));
    result = pressAscii(core, editor, QChar(u'b'));
    QVERIFY(!lastActivationRequest(result));

    const BufferId vaultSwitchBuffer =
        core.synchronizeBuffer(
            editor,
            "/next-vault/Note.txt",
            u"next",
            {0, 0});
    QVERIFY(vaultSwitchBuffer != 0);
    core.clearWorkspace();
    QCOMPARE(core.bufferCount(), std::size_t{0});
    QVERIFY(!core.bufferForPath(
        "/next-vault/Note.txt"));
    QVERIFY(!core.activeBuffer(editor));
}

void VkCoreTests::
    bufferDestructionRevokesOnlyItsLocalInputContributions()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId first = core.synchronizeBuffer(
        window, "/vault/remove/first.txt", u"first", {0, 0});
    const BufferId second = core.synchronizeBuffer(
        window, "/vault/remove/second.txt", u"second", {0, 0});
    const BufferId peer = core.synchronizeBuffer(
        window, "/vault/peer.txt", u"peer", {0, 0});
    QVERIFY(first != 0 && second != 0 && peer != 0);

    std::string error;
    const MappingId globalMapping = core.addMapping(
        actionMapping(U"z", HostAction::FocusPanelLeft), &error);
    const auto addLocalMapping =
        [&core, &error](const BufferId buffer) {
            MappingDefinition mapping = actionMapping(
                U"x", HostAction::FocusPanelRight);
            mapping.buffer = buffer;
            return core.addMapping(mapping, &error);
        };
    const MappingId firstMapping = addLocalMapping(first);
    const MappingId secondMapping = addLocalMapping(second);
    const MappingId peerMapping = addLocalMapping(peer);
    QVERIFY(globalMapping != 0 && firstMapping != 0
            && secondMapping != 0 && peerMapping != 0);

    auto globalCommand = core.userCommands().registerCommand(
        "plugin.global",
        UserCommandDefinition{
            "Inspect", "plugin.global.inspect", "Global"},
        &error);
    const auto addLocalCommand =
        [&core, &error](
            const BufferId buffer,
            std::string owner,
            std::string target) {
            UserCommandDefinition definition{
                "Inspect", std::move(target), "Local"};
            definition.buffer = buffer;
            return core.userCommands().registerCommand(
                std::move(owner),
                std::move(definition),
                &error);
        };
    auto firstCommand = addLocalCommand(
        first, "plugin.first", "plugin.first.inspect");
    auto secondCommand = addLocalCommand(
        second, "plugin.second", "plugin.second.inspect");
    auto peerCommand = addLocalCommand(
        peer, "plugin.peer", "plugin.peer.inspect");
    QVERIFY(globalCommand && firstCommand
            && secondCommand && peerCommand);

    QCOMPARE(
        core.removeBuffersUnderPath("/vault/remove"),
        std::size_t{2});
    QVERIFY(!firstCommand.valid());
    QVERIFY(!secondCommand.valid());
    QVERIFY(peerCommand.valid());
    QVERIFY(globalCommand.valid());
    QCOMPARE(
        core.removeMappings(
            std::array{firstMapping, secondMapping})
            .removed,
        std::size_t{0});
    QCOMPARE(
        core.userCommands().definition("Inspect", first)->target,
        std::string("plugin.global.inspect"));
    QCOMPARE(
        core.userCommands().definition("Inspect", peer)->target,
        std::string("plugin.peer.inspect"));
    QVERIFY(hasHostAction(
        pressAscii(core, window, QChar(u'x')),
        HostAction::FocusPanelRight));

    QVERIFY(core.removeBuffer(peer));
    QVERIFY(!peerCommand.valid());
    QVERIFY(!core.removeMapping(peerMapping));

    const BufferId clearBuffer = core.synchronizeBuffer(
        window, "/next-vault/clear.txt", u"clear", {0, 0});
    const MappingId clearMapping = addLocalMapping(clearBuffer);
    auto clearCommand = addLocalCommand(
        clearBuffer, "plugin.clear", "plugin.clear.inspect");
    QVERIFY(clearMapping != 0 && clearCommand);
    core.clearWorkspace();
    QVERIFY(!clearCommand.valid());
    QVERIFY(!core.removeMapping(clearMapping));
    QVERIFY(globalCommand.valid());
    QVERIFY(core.userCommands().contains("Inspect"));
    QVERIFY(hasHostAction(
        pressAscii(core, window, QChar(u'z')),
        HostAction::FocusPanelLeft));
}

void VkCoreTests::boundedBufferReadsCarryRevisionAndPagingState()
{
    VkCore core;
    const WindowId window = core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/book.txt",
        u"chapter one\nchapter two",
        {});
    QVERIFY(buffer != 0);

    const auto first = core.readBufferRange(buffer, 0, 7);
    QVERIFY(first.has_value());
    QCOMPARE(first->id, buffer);
    QCOMPARE(first->offset, std::size_t{0});
    QCOMPARE(first->totalSize, std::size_t{23});
    QCOMPARE(first->text, std::u16string(u"chapter"));
    QVERIFY(!first->atEnd());

    const auto descriptor = core.bufferDataDescriptor(buffer);
    QVERIFY(descriptor.has_value());
    QCOMPARE(descriptor->size, std::size_t{23});
    QCOMPARE(descriptor->revision, first->revision);
    QVERIFY(descriptor->editable);
    QVERIFY(!descriptor->identity.value.empty());
    const auto exactRevision = core.readBufferRangeAtRevision(
        buffer, 8, 3, descriptor->revision);
    QVERIFY(exactRevision.has_value());
    QCOMPARE(exactRevision->text, std::u16string(u"one"));

    const auto tail = core.readBufferRange(buffer, 20, 64);
    QVERIFY(tail.has_value());
    QCOMPARE(tail->offset, std::size_t{20});
    QCOMPARE(tail->text, std::u16string(u"two"));
    QVERIFY(tail->atEnd());
    QCOMPARE(tail->revision, first->revision);

    QVERIFY(core.replaceBufferText(
        buffer, u"revised", {}));
    const auto revised = core.readBufferRange(buffer, 0, 64);
    QVERIFY(revised.has_value());
    QVERIFY(revised->revision > first->revision);
    QCOMPARE(revised->text, std::u16string(u"revised"));
    QVERIFY(revised->atEnd());
    QVERIFY(!core.readBufferRangeAtRevision(
                 buffer, 0, 64, first->revision)
                 .has_value());
    QVERIFY(!core.readBufferRange(999'999, 0, 1).has_value());
}

void VkCoreTests::
    providerBuffersExposeBoundedDataWithoutUnsafeAttachment()
{
    VkCore core;
    const WindowId window = core.registerWindow(WindowKind::Surface);
    auto provider = std::make_shared<CorePagedProvider>();
    const auto registered = core.registerProviderBuffer(
        "/vault/large.publication",
        provider,
        16);
    QVERIFY(registered.registered());
    QCOMPARE(
        registered.status,
        BufferRegistrationStatus::Created);
    QVERIFY(registered.buffer != 0);

    const auto descriptor = core.bufferDataDescriptor(
        registered.buffer);
    QVERIFY(descriptor.has_value());
    QCOMPARE(descriptor->size, CorePagedProvider::sourceSize);
    QCOMPARE(descriptor->revision, std::uint64_t{31});
    QVERIFY(!descriptor->editable);

    const auto pending = core.readBufferDataRange(
        registered.buffer,
        vkui::buffer::RangeRequest{4096, 1000, 31});
    QCOMPARE(
        pending.status,
        vkui::buffer::ReadStatus::Pending);
    QVERIFY(pending.text.empty());
    QCOMPARE(provider->largestRead, std::size_t{16});
    QVERIFY(!core.readBufferRange(
                 registered.buffer, 4096, 16)
                 .has_value());
    // The compatibility whole-text API is deliberately unavailable instead
    // of materializing 128 Mi UTF-16 code units.
    QVERIFY(!core.buffer(registered.buffer).has_value());

    QCOMPARE(
        core.attachBufferData(window, registered.buffer, {}),
        BufferAttachStatus::RequiresResidentTextProjection);
    QVERIFY(!core.attachBuffer(window, registered.buffer, {}));
    QVERIFY(core.window(window).has_value());
    QCOMPARE(core.window(window)->buffer, BufferId{0});
    QVERIFY(!core.replaceBufferText(
        registered.buffer, u"must not replace", {}));
    QVERIFY(!core.bufferOffset(
                 registered.buffer, {})
                 .has_value());

    bool ready = false;
    std::stop_source stop;
    QVERIFY(core.requestBufferPrefetch(
        registered.buffer,
        vkui::buffer::PrefetchRequest{
            4096,
            CorePagedProvider::sourceSize,
            31,
            700},
        stop.get_token(),
        [&ready](const vkui::buffer::PrefetchResult result) {
            ready = result.status
                    == vkui::buffer::PrefetchStatus::Ready
                && result.generation == 700
                && result.availableLength == 16;
        }));
    QVERIFY(ready);
    QCOMPARE(provider->largestPrefetch, std::size_t{16});

    const auto resident = core.readBufferDataRange(
        registered.buffer,
        vkui::buffer::RangeRequest{4096, 1000, 31});
    QVERIFY(resident.ok());
    QCOMPARE(resident.text.size(), std::size_t{16});
    const auto compatible = core.readBufferRangeAtRevision(
        registered.buffer, 4096, 1000, 31);
    QVERIFY(compatible.has_value());
    QCOMPARE(compatible->text.size(), std::size_t{16});

    const auto duplicate = core.registerProviderBuffer(
        "/vault/renamed.publication", provider, 16);
    QCOMPARE(
        duplicate.status,
        BufferRegistrationStatus::AlreadyRegistered);
    QCOMPARE(duplicate.buffer, registered.buffer);
    QVERIFY(core.removeBuffer(registered.buffer));
}

QTEST_GUILESS_MAIN(VkCoreTests)

#include "tst_interaction.moc"
