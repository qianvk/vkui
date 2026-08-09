#include <vkui/vk/VkCore.h>
#include <vkui/vk/VkUserCommandRegistry.h>

#include <QTest>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

using namespace vkui::vk;

class VkUserCommandRegistryTests final : public QObject
{
    Q_OBJECT

private slots:
    void validatesAndResolvesInvocationMetadata();
    void bufferLocalCommandsOverrideGlobalCommands();
    void batchPublicationAndLeasesAreOwnerExact();
    void deletedBufferAliasesAreRevokedWithoutTouchingPeers();
    void exSubmissionUsesAliasesAndRejectsUnknownCommands();
};

void VkUserCommandRegistryTests::
    validatesAndResolvesInvocationMetadata()
{
    VkUserCommandRegistry registry;
    std::string error;
    UserCommandDefinition definition;
    definition.name = "DoThing";
    definition.target = "plugin.example.run";
    definition.description = "Run the example";
    definition.nargs = UserCommandNargs::OneOrMore;
    definition.acceptsBang = true;
    definition.acceptsRange = true;
    definition.acceptsCount = true;
    auto lease = registry.registerCommand(
        "plugin.example", std::move(definition), &error);
    QVERIFY2(lease, error.c_str());

    CommandInvocation context;
    context.mode = Mode::Visual;
    context.window = 9;
    context.buffer = 41;
    context.inputTarget = 77;
    auto resolved = registry.resolve(
        UserCommandRequest{
            "DoThing",
            {"alpha", "beta"},
            "alpha beta",
            true,
            CommandLineRange{2, 6},
            std::size_t{4},
            context},
        &error);
    QVERIFY2(resolved.has_value(), error.c_str());
    QCOMPARE(resolved->invocation.id, std::string("plugin.example.run"));
    QCOMPARE(
        resolved->invocation.arguments,
        std::vector<std::string>({"alpha", "beta"}));
    QCOMPARE(resolved->invocation.window, WindowId{9});
    QCOMPARE(resolved->invocation.buffer, BufferId{41});
    QCOMPARE(resolved->invocation.inputTarget, InputTargetId{77});
    QCOMPARE(resolved->invocation.mode, Mode::Visual);
    QCOMPARE(resolved->invocation.count, std::size_t{4});
    QVERIFY(resolved->invocation.countWasExplicit);
    QVERIFY(resolved->invocation.bang);
    QCOMPARE(
        resolved->invocation.lineRange,
        std::optional<CommandLineRange>({2, 6}));
    QCOMPARE(resolved->invocation.rawArguments, std::string("alpha beta"));

    auto invalid = registry.resolve(
        UserCommandRequest{"DoThing", {}, {}, false, {}, {}, context},
        &error);
    QVERIFY(!invalid.has_value());
    QVERIFY(error.find("number of arguments") != std::string::npos);
    QVERIFY(!VkUserCommandRegistry::isValidName("lowercase"));
    QVERIFY(VkUserCommandRegistry::isValidName("Upper123"));
}

void VkUserCommandRegistryTests::
    bufferLocalCommandsOverrideGlobalCommands()
{
    VkUserCommandRegistry registry;
    std::string error;
    auto global = registry.registerCommand(
        "plugin.global",
        UserCommandDefinition{
            "Inspect", "plugin.global.inspect", "Inspect globally"},
        &error);
    QVERIFY2(global, error.c_str());

    UserCommandDefinition localDefinition{
        "Inspect", "plugin.local.inspect", "Inspect this buffer"};
    localDefinition.buffer = BufferId{12};
    auto local = registry.registerCommand(
        "plugin.local", std::move(localDefinition), &error);
    QVERIFY2(local, error.c_str());

    CommandInvocation localContext;
    localContext.buffer = 12;
    auto resolved = registry.resolve(
        UserCommandRequest{
            "Inspect", {}, {}, false, {}, {}, localContext},
        &error);
    QVERIFY2(resolved.has_value(), error.c_str());
    QCOMPARE(resolved->invocation.id, std::string("plugin.local.inspect"));

    CommandInvocation globalContext;
    globalContext.buffer = 13;
    resolved = registry.resolve(
        UserCommandRequest{
            "Inspect", {}, {}, false, {}, {}, globalContext},
        &error);
    QVERIFY2(resolved.has_value(), error.c_str());
    QCOMPARE(resolved->invocation.id, std::string("plugin.global.inspect"));
}

void VkUserCommandRegistryTests::
    batchPublicationAndLeasesAreOwnerExact()
{
    VkUserCommandRegistry registry;
    std::string error;
    auto leases = registry.registerCommands(
        "plugin.one",
        {
            {"First", "plugin.one.first", "First"},
            {"Second", "plugin.one.second", "Second"},
        },
        &error);
    QCOMPARE(leases.size(), std::size_t{2});

    auto conflicting = registry.registerCommands(
        "plugin.two",
        {
            {"Third", "plugin.two.third", "Third"},
            {"First", "plugin.two.first", "Collision"},
        },
        &error);
    QVERIFY(conflicting.empty());
    QVERIFY(!registry.contains("Third"));
    QVERIFY(registry.contains("First"));

    QCOMPARE(registry.revokeOwner("plugin.two"), std::size_t{0});
    QVERIFY(registry.contains("First"));
    QCOMPARE(registry.revokeOwner("plugin.one"), std::size_t{2});
    QVERIFY(!registry.contains("First"));
    QVERIFY(!leases.front().valid());
}

void VkUserCommandRegistryTests::
    deletedBufferAliasesAreRevokedWithoutTouchingPeers()
{
    VkUserCommandRegistry registry;
    std::string error;
    auto global = registry.registerCommand(
        "plugin.global",
        UserCommandDefinition{
            "Inspect", "plugin.global.inspect", "Global"},
        &error);
    QVERIFY2(global, error.c_str());
    UserCommandDefinition removed{
        "Inspect", "plugin.local.inspect", "Removed buffer"};
    removed.buffer = BufferId{12};
    auto removedLease = registry.registerCommand(
        "plugin.local", std::move(removed), &error);
    QVERIFY2(removedLease, error.c_str());
    UserCommandDefinition peer{
        "Inspect", "plugin.peer.inspect", "Peer buffer"};
    peer.buffer = BufferId{13};
    auto peerLease = registry.registerCommand(
        "plugin.peer", std::move(peer), &error);
    QVERIFY2(peerLease, error.c_str());
    UserCommandDefinition secondRemoved{
        "Inspect", "plugin.second.inspect", "Second removed buffer"};
    secondRemoved.buffer = BufferId{14};
    auto secondRemovedLease = registry.registerCommand(
        "plugin.second", std::move(secondRemoved), &error);
    QVERIFY2(secondRemovedLease, error.c_str());

    const std::array removedBuffers{
        BufferId{12}, BufferId{14}, BufferId{12}};
    QCOMPARE(
        registry.revokeBuffers(removedBuffers),
        std::size_t{2});
    QVERIFY(!removedLease.valid());
    QVERIFY(!secondRemovedLease.valid());
    QVERIFY(peerLease.valid());
    QVERIFY(global.valid());
    QCOMPARE(
        registry.definition("Inspect", BufferId{12})->target,
        std::string("plugin.global.inspect"));
    QCOMPARE(
        registry.definition("Inspect", BufferId{13})->target,
        std::string("plugin.peer.inspect"));
}

void VkUserCommandRegistryTests::
    exSubmissionUsesAliasesAndRejectsUnknownCommands()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window, "/vault/test.txt", u"text", {0, 0});
    QVERIFY(buffer != 0);

    std::string error;
    UserCommandDefinition definition{
        "DoThing", "plugin.example.run", "Run"};
    definition.nargs = UserCommandNargs::OneOrMore;
    definition.acceptsBang = true;
    auto lease = core.userCommands().registerCommand(
        "plugin.example", std::move(definition), &error);
    QVERIFY2(lease, error.c_str());

    const DispatchResult result = core.submitCommandLine(
        window,
        CommandLineKind::Ex,
        u":DoThing! alpha\\ beta gamma");
    const auto requested = std::ranges::find_if(
        result.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        });
    QVERIFY(requested != result.events.cend());
    QCOMPARE(requested->commandId, std::string("plugin.example.run"));
    QCOMPARE(
        requested->commandArguments,
        std::vector<std::string>({"alpha beta", "gamma"}));
    QCOMPARE(requested->buffer, buffer);
    QCOMPARE(requested->view, window);
    QVERIFY(requested->bang);
    QCOMPARE(requested->rawArguments, std::string("alpha\\ beta gamma"));

    const DispatchResult unknown = core.submitCommandLine(
        window, CommandLineKind::Ex, u":Missing");
    QVERIFY(std::ranges::any_of(
        unknown.events,
        [](const Event &event) {
            return event.type == EventType::InputError
                && event.message.find("not registered")
                    != std::string::npos;
        }));
    QVERIFY(std::ranges::none_of(
        unknown.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        }));
}

QTEST_GUILESS_MAIN(VkUserCommandRegistryTests)

#include "tst_interaction_user_commands.moc"
