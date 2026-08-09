#include <vkui/vk/VkCore.h>

#include <QKeyEvent>
#include <QTest>

#include <algorithm>
#include <optional>
#include <string>

using namespace vkui::vk;

namespace {

[[nodiscard]] QKeyEvent key(
    const int value,
    const QString &text)
{
    return QKeyEvent(
        QEvent::KeyPress,
        value,
        Qt::NoModifier,
        text);
}

[[nodiscard]] bool hasCommand(
    const DispatchResult &result,
    const std::string_view id)
{
    return std::ranges::any_of(
        result.events,
        [id](const Event &event) {
            return event.type
                    == EventType::CommandRequested
                && event.commandId == id;
        });
}

[[nodiscard]] MappingDefinition commandMapping(
    std::u32string lhs,
    std::string command,
    std::string description)
{
    MappingDefinition definition;
    definition.lhs = std::move(lhs);
    definition.target =
        CommandTarget{std::move(command)};
    definition.options.remap =
        RemapPolicy::NoRemap;
    definition.metadata =
        MappingMetadata{
            std::move(description),
            "Test",
            "vkery.test",
            false,
            0};
    return definition;
}

} // namespace

class VkCommandDispatchTests final : public QObject
{
    Q_OBJECT

private slots:
    void semanticCommandSuspendsAndResumesTypeahead();
    void rejectedCommandDiscardsMappingTail();
    void coreForwardsTheLivePrefixSnapshot();
};

void VkCommandDispatchTests::
semanticCommandSuspendsAndResumesTypeahead()
{
    VkCore core;
    const WindowId window =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/test.txt",
        u"one\ntwo\nthree",
        {});
    QVERIFY(buffer != 0);

    std::string error;
    QVERIFY2(
        core.addMapping(
            commandMapping(
                U"q",
                "vkery.test.command",
                "Test command"),
            &error)
            != 0,
        error.c_str());
    MappingDefinition transaction;
    transaction.lhs = U"x";
    transaction.target =
        KeySequenceTarget{U"qj"};
    transaction.options.remap =
        RemapPolicy::Recursive;
    QVERIFY2(
        core.addMapping(transaction, &error) != 0,
        error.c_str());

    const QKeyEvent input =
        key(Qt::Key_X, QStringLiteral("x"));
    const DispatchResult dispatched =
        core.dispatch(window, 1, input);
    QVERIFY(hasCommand(
        dispatched, "vkery.test.command"));
    QVERIFY(dispatched.hostBarrierGeneration.has_value());
    QCOMPARE(
        core.window(window)->cursor,
        Cursor{});

    const DispatchResult resumed =
        core.acknowledgeHostBarrier(
            *dispatched.hostBarrierGeneration,
            true,
            window,
            1);
    QVERIFY(!resumed.hostBarrierGeneration);
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{1, 0}));
}

void VkCommandDispatchTests::
rejectedCommandDiscardsMappingTail()
{
    VkCore core;
    const WindowId window =
        core.registerView(ViewKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        window,
        "/test.txt",
        u"one\ntwo",
        {})
        != 0);

    std::string error;
    QVERIFY(
        core.addMapping(
            commandMapping(
                U"q",
                "vkery.test.command",
                "Test command"),
            &error)
        != 0);
    MappingDefinition transaction;
    transaction.lhs = U"x";
    transaction.target =
        KeySequenceTarget{U"qj"};
    QVERIFY(
        core.addMapping(transaction, &error) != 0);

    const QKeyEvent input =
        key(Qt::Key_X, QStringLiteral("x"));
    const DispatchResult dispatched =
        core.dispatch(window, 1, input);
    QVERIFY(dispatched.hostBarrierGeneration);
    const DispatchResult rejected =
        core.acknowledgeHostBarrier(
            *dispatched.hostBarrierGeneration,
            false,
            window,
            1);
    QVERIFY(rejected.events.empty());
    QCOMPARE(
        core.window(window)->cursor,
        Cursor{});
}

void VkCommandDispatchTests::
coreForwardsTheLivePrefixSnapshot()
{
    VkCore core;
    const WindowId window =
        core.registerView(ViewKind::Navigation);
    std::string error;
    QVERIFY(
        core.addMapping(
            commandMapping(
                U"<Leader>e",
                "vkery.vktree.toggle",
                "Toggle Explorer"),
            &error)
        != 0);
    QVERIFY(
        core.addMapping(
            commandMapping(
                U"<Leader>ps",
                "vkery.preferences.plugins",
                "Open Plugin Manager"),
            &error)
        != 0);

    const QKeyEvent leader =
        key(Qt::Key_Space, QStringLiteral(" "));
    const DispatchResult pending =
        core.dispatch(window, 0, leader);
    QVERIFY(pending.inputHintGeneration);
    QVERIFY(!pending.mappingDeadlineGeneration);
    const auto snapshot =
        core.pendingInputHint(
            *pending.inputHintGeneration);
    QVERIFY(snapshot.has_value());
    QCOMPARE(
        snapshot->generation,
        *pending.inputHintGeneration);
    QCOMPARE(
        snapshot->waitPolicy,
        InputHintWaitPolicy::PersistentLeader);
    QCOMPARE(snapshot->candidates.size(), std::size_t{2});
    QVERIFY(std::ranges::any_of(
        snapshot->candidates,
        [](const MappingPrefixCandidate &candidate) {
            return candidate.keyNotation == U"e"
                && candidate.commandId
                    == "vkery.vktree.toggle";
        }));

    (void)core.flushPendingInput();
    QVERIFY(!core.pendingInputHint(
        *pending.inputHintGeneration));
}

QTEST_GUILESS_MAIN(VkCommandDispatchTests)
#include "tst_interaction_dispatch.moc"
