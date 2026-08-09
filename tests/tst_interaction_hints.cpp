#include <vkui/vk/VkInputEngine.h>

#include <QTest>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

using namespace vkui::vk;
using namespace vkui::vk::detail;

namespace {

[[nodiscard]] MappingDefinition commandMapping(
    std::u32string lhs,
    std::string command,
    MappingMetadata metadata,
    const std::optional<BufferId> buffer = std::nullopt,
    const MappingModes modes =
        mappingModes(MappingMode::Normal),
    const std::optional<WindowId> window = std::nullopt)
{
    MappingDefinition definition;
    definition.modes = modes;
    definition.lhs = std::move(lhs);
    definition.target =
        CommandTarget{std::move(command)};
    definition.options =
        MappingOptions{
            RemapPolicy::NoRemap,
            false,
            true};
    definition.buffer = buffer;
    definition.metadata = std::move(metadata);
    definition.window = window;
    return definition;
}

[[nodiscard]] MappingMetadata metadata(
    std::string description,
    std::string group,
    std::string plugin,
    const bool hidden,
    const int order)
{
    return MappingMetadata{
        std::move(description),
        std::move(group),
        std::move(plugin),
        hidden,
        order};
}

[[nodiscard]] MappingDefinition actionMapping(
    std::u32string lhs,
    const HostAction action,
    const bool nowait = false,
    const std::optional<BufferId> buffer = std::nullopt)
{
    MappingDefinition definition;
    definition.lhs = std::move(lhs);
    definition.target = HostActionTarget{action};
    definition.options =
        MappingOptions{
            RemapPolicy::NoRemap,
            nowait,
            true};
    definition.buffer = buffer;
    return definition;
}

[[nodiscard]] const MappingPrefixCandidate *candidate(
    const MappingPrefixSnapshot &snapshot,
    const char32_t key)
{
    const auto found = std::ranges::find_if(
        snapshot.candidates,
        [key](const MappingPrefixCandidate &value) {
            return value.keyNotation
                == std::u32string(1, key);
        });
    return found == snapshot.candidates.cend()
        ? nullptr
        : &*found;
}

void beginLeaderPrefix(
    InputEngine &engine,
    const MappingMode mode,
    const std::optional<BufferId> buffer = std::nullopt)
{
    engine.appendTyped(characterKey(U' '));
    const ResolveStep step =
        engine.resolve(mode, buffer, false);
    QVERIFY(step.kind == ResolveKind::NeedMore);
}

} // namespace

class VkInputEngineHintsTests final : public QObject
{
    Q_OBJECT

private slots:
    void leaderCandidatesExposeCanonicalMetadata();
    void bufferLocalCandidateOverridesGlobalByNextKey();
    void windowLocalCandidateOverridesBufferAndGlobal();
    void hiddenRemoveAndRedefinitionStayLive();
    void recursiveRhsSnapshotUsesActualTypeahead();
    void modeIndexAndCommandResolutionAreExact();
    void triePreservesLongestMatchAndNowait();
    void emptyHostActionIsRejected();
    void mappingBatchPublishesOnceAndRollsBackExactly();
    void deletedBufferMappingsAreReclaimedAsOneBatch();
    void repeatPrefixCreatesInterruptibleCommandSubmode();
};

void VkInputEngineHintsTests::
    leaderCandidatesExposeCanonicalMetadata()
{
    InputEngine engine;
    engine.setLeader(
        KeySequence{characterKey(U' ')});
    std::string error;
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>e",
                "vkery.vktree.toggle",
                metadata(
                    "Toggle file tree",
                    "Explorer",
                    "vkery.vktree",
                    false,
                    20)),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader><C-x>",
                "vkery.tools.control-x",
                metadata(
                    "Control command",
                    "Tools",
                    "vkery.tools",
                    false,
                    30)),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>ps",
                "vkery.plugins.open",
                metadata(
                    "Open plugin manager",
                    "Plugins",
                    "vkery.plugin-manager",
                    false,
                    10)),
            &error)
            != 0,
        error.c_str());

    beginLeaderPrefix(engine, MappingMode::Normal);
    const auto snapshot =
        engine.pendingPrefixSnapshot(
            MappingMode::Normal,
            std::nullopt,
            73);
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->generation, std::uint64_t{73});
    QCOMPARE(
        snapshot->waitPolicy,
        InputHintWaitPolicy::PersistentLeader);
    QVERIFY(snapshot->prefixNotation == U"<Space>");
    QCOMPARE(snapshot->candidates.size(), std::size_t{3});

    // Explicit order is stable even though trie children use a hash table.
    QVERIFY(snapshot->candidates[0].keyNotation == U"p");
    QVERIFY(snapshot->candidates[1].keyNotation == U"e");
    QVERIFY(
        snapshot->candidates[2].keyNotation == U"<C-x>");
    QVERIFY(
        snapshot->candidates[2].sequenceNotation
        == U"<Space><C-x>");

    const MappingPrefixCandidate *const plugins =
        candidate(*snapshot, U'p');
    QVERIFY(plugins != nullptr);
    QVERIFY(!plugins->completesMapping);
    QVERIFY(plugins->hasChildren);
    QCOMPARE(plugins->group, std::string("Plugins"));
    QVERIFY(plugins->commandId.empty());

    const MappingPrefixCandidate *const explorer =
        candidate(*snapshot, U'e');
    QVERIFY(explorer != nullptr);
    QVERIFY(explorer->sequenceNotation == U"<Space>e");
    QVERIFY(explorer->completesMapping);
    QVERIFY(!explorer->hasChildren);
    QCOMPARE(
        explorer->description,
        std::string("Toggle file tree"));
    QCOMPARE(explorer->group, std::string("Explorer"));
    QCOMPARE(
        explorer->sourcePlugin,
        std::string("vkery.vktree"));
    QCOMPARE(
        explorer->commandId,
        std::string("vkery.vktree.toggle"));
    QVERIFY(!explorer->hidden);
    QCOMPARE(explorer->order, 20);
}

void VkInputEngineHintsTests::
    bufferLocalCandidateOverridesGlobalByNextKey()
{
    InputEngine engine;
    engine.setLeader(
        KeySequence{characterKey(U' ')});
    std::string error;
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>e",
                "vkery.global.explorer",
                metadata(
                    "Global explorer",
                    "Explorer",
                    "plugin.global",
                    false,
                    20)),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>p",
                "vkery.global.plugins",
                metadata(
                    "Global plugins",
                    "Plugins",
                    "plugin.global",
                    false,
                    30)),
            &error)
            != 0,
        error.c_str());
    constexpr BufferId localBuffer = 42;
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>e",
                "vkery.local.explorer",
                metadata(
                    "Local explorer",
                    "Explorer",
                    "plugin.local",
                    false,
                    10),
                localBuffer),
            &error)
            != 0,
        error.c_str());

    beginLeaderPrefix(
        engine,
        MappingMode::Normal,
        localBuffer);
    const auto local =
        engine.pendingPrefixSnapshot(
            MappingMode::Normal,
            localBuffer,
            1);
    QVERIFY(local.has_value());
    QCOMPARE(local->candidates.size(), std::size_t{2});
    const MappingPrefixCandidate *const localExplorer =
        candidate(*local, U'e');
    QVERIFY(localExplorer != nullptr);
    QVERIFY(localExplorer->bufferLocal);
    QCOMPARE(
        localExplorer->description,
        std::string("Local explorer"));
    QCOMPARE(
        localExplorer->commandId,
        std::string("vkery.local.explorer"));
    QVERIFY(candidate(*local, U'p') != nullptr);

    engine.clear();
    beginLeaderPrefix(
        engine,
        MappingMode::Normal,
        BufferId{7});
    const auto global =
        engine.pendingPrefixSnapshot(
            MappingMode::Normal,
            BufferId{7},
            2);
    QVERIFY(global.has_value());
    const MappingPrefixCandidate *const globalExplorer =
        candidate(*global, U'e');
    QVERIFY(globalExplorer != nullptr);
    QVERIFY(!globalExplorer->bufferLocal);
    QCOMPARE(
        globalExplorer->description,
        std::string("Global explorer"));
}

void VkInputEngineHintsTests::
    windowLocalCandidateOverridesBufferAndGlobal()
{
    InputEngine engine;
    engine.setLeader(
        KeySequence{characterKey(U' ')});
    constexpr BufferId buffer = 42;
    constexpr WindowId window = 9;
    std::string error;
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>e",
                "vkery.global.explorer",
                metadata(
                    "Global explorer",
                    "Explorer",
                    "plugin.global",
                    false,
                    30)),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>p",
                "vkery.global.preferences",
                metadata(
                    "Global preferences",
                    "Application",
                    "plugin.global",
                    false,
                    -100)),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>e",
                "vkery.buffer.explorer",
                metadata(
                    "Buffer explorer",
                    "Explorer",
                    "plugin.buffer",
                    false,
                    20),
                buffer),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>e",
                "vkery.window.explorer",
                metadata(
                    "Window explorer",
                    "Explorer",
                    "plugin.window",
                    false,
                    10),
                std::nullopt,
                mappingModes(MappingMode::Normal),
                window),
            &error)
            != 0,
        error.c_str());

    engine.appendTyped(characterKey(U' '));
    const ResolveStep waiting = engine.resolve(
        MappingMode::Normal,
        buffer,
        false,
        window);
    QCOMPARE(waiting.kind, ResolveKind::NeedMore);
    const auto snapshot =
        engine.pendingPrefixSnapshot(
            MappingMode::Normal,
            buffer,
            3,
            window);
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->window, std::optional<WindowId>(window));
    QCOMPARE(snapshot->candidates.size(), std::size_t{2});
    // which-key's local sorter wins before explicit order. A panel command
    // must therefore precede a common global command even when the global
    // command requested an earlier manual order.
    QVERIFY(snapshot->candidates.front().keyNotation == U"e");
    const MappingPrefixCandidate *const explorer =
        candidate(*snapshot, U'e');
    QVERIFY(explorer != nullptr);
    QVERIFY(explorer->windowLocal);
    QVERIFY(!explorer->bufferLocal);
    QCOMPARE(
        explorer->commandId,
        std::string("vkery.window.explorer"));

    engine.appendTyped(characterKey(U'e'));
    const ResolveStep command = engine.resolve(
        MappingMode::Normal,
        buffer,
        false,
        window);
    QCOMPARE(command.kind, ResolveKind::Command);
    QCOMPARE(
        command.commandId,
        std::string("vkery.window.explorer"));

    QCOMPARE(
        engine.removeWindowMappings(window),
        std::size_t{1});
    beginLeaderPrefix(
        engine,
        MappingMode::Normal,
        buffer);
    const auto afterRemoval =
        engine.pendingPrefixSnapshot(
            MappingMode::Normal,
            buffer,
            4,
            window);
    QVERIFY(afterRemoval.has_value());
    const MappingPrefixCandidate *const fallback =
        candidate(*afterRemoval, U'e');
    QVERIFY(fallback != nullptr);
    QVERIFY(fallback->bufferLocal);
    QVERIFY(!fallback->windowLocal);
    QCOMPARE(
        fallback->commandId,
        std::string("vkery.buffer.explorer"));
}

void VkInputEngineHintsTests::
    hiddenRemoveAndRedefinitionStayLive()
{
    InputEngine engine;
    engine.setLeader(
        KeySequence{characterKey(U' ')});
    std::string error;
    const MappingId hidden = engine.addMapping(
        commandMapping(
            U"<Leader>p",
            "vkery.plugins.hidden",
            metadata(
                "Internal plugin action",
                "Plugins",
                "plugin.hidden",
                true,
                30)),
        &error);
    QVERIFY2(hidden != 0, error.c_str());
    const MappingId original = engine.addMapping(
        commandMapping(
            U"<Leader>e",
            "vkery.explorer.old",
            metadata(
                "Old explorer",
                "Explorer",
                "plugin.old",
                false,
                20)),
        &error);
    QVERIFY2(original != 0, error.c_str());

    beginLeaderPrefix(engine, MappingMode::Normal);
    auto snapshot =
        engine.pendingPrefixSnapshot(
            MappingMode::Normal,
            std::nullopt,
            1);
    QVERIFY(snapshot.has_value());
    const MappingPrefixCandidate *hiddenCandidate =
        candidate(*snapshot, U'p');
    QVERIFY(hiddenCandidate != nullptr);
    QVERIFY(hiddenCandidate->hidden);

    QVERIFY(engine.removeMapping(hidden));
    snapshot = engine.pendingPrefixSnapshot(
        MappingMode::Normal,
        std::nullopt,
        2);
    QVERIFY(snapshot.has_value());
    QVERIFY(candidate(*snapshot, U'p') == nullptr);

    const MappingId replacement = engine.addMapping(
        commandMapping(
            U"<Leader>e",
            "vkery.explorer.new",
            metadata(
                "New explorer",
                "Explorer",
                // Redefinition is owner-scoped. A different plugin must not
                // silently replace another owner's executable mapping merely
                // to refresh which-key presentation metadata.
                "plugin.old",
                false,
                5)),
        &error);
    QVERIFY2(replacement != 0, error.c_str());
    snapshot = engine.pendingPrefixSnapshot(
        MappingMode::Normal,
        std::nullopt,
        3);
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->candidates.size(), std::size_t{1});
    const MappingPrefixCandidate *const redefined =
        candidate(*snapshot, U'e');
    QVERIFY(redefined != nullptr);
    QCOMPARE(redefined->mappingId, replacement);
    QCOMPARE(
        redefined->description,
        std::string("New explorer"));
    QCOMPARE(
        redefined->commandId,
        std::string("vkery.explorer.new"));

    QVERIFY(engine.removeMapping(replacement));
    QVERIFY(
        !engine.pendingPrefixSnapshot(
             MappingMode::Normal,
             std::nullopt,
             4)
             .has_value());
}

void VkInputEngineHintsTests::
    recursiveRhsSnapshotUsesActualTypeahead()
{
    InputEngine engine;
    engine.setLeader(
        KeySequence{characterKey(U' ')});
    std::string error;
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>e",
                "vkery.vktree.toggle",
                metadata(
                    "Toggle file tree",
                    "Explorer",
                    "vkery.vktree",
                    false,
                    10)),
            &error)
            != 0,
        error.c_str());

    MappingDefinition recursive;
    recursive.modes =
        mappingModes(MappingMode::Normal);
    recursive.lhs = U"x";
    recursive.target =
        KeySequenceTarget{U"<Leader>"};
    recursive.options =
        MappingOptions{
            RemapPolicy::Recursive,
            false,
            false};
    QVERIFY2(
        engine.addMapping(recursive, &error) != 0,
        error.c_str());

    engine.appendTyped(characterKey(U'x'));
    const ResolveStep waiting =
        engine.resolve(
            MappingMode::Normal,
            std::nullopt,
            false);
    QVERIFY(waiting.kind == ResolveKind::NeedMore);
    const auto snapshot =
        engine.pendingPrefixSnapshot(
            MappingMode::Normal,
            std::nullopt,
            99);
    QVERIFY(snapshot.has_value());
    QVERIFY(snapshot->prefixNotation == U"<Space>");
    QVERIFY(candidate(*snapshot, U'e') != nullptr);

    engine.appendTyped(characterKey(U'e'));
    const ResolveStep command =
        engine.resolve(
            MappingMode::Normal,
            std::nullopt,
            false);
    QVERIFY(command.kind == ResolveKind::Command);
    QCOMPARE(
        command.commandId,
        std::string("vkery.vktree.toggle"));
}

void VkInputEngineHintsTests::
    modeIndexAndCommandResolutionAreExact()
{
    InputEngine engine;
    engine.setLeader(
        KeySequence{characterKey(U' ')});
    std::string error;
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>e",
                "vkery.normal.explorer",
                metadata(
                    "Normal explorer",
                    "Explorer",
                    "plugin.normal",
                    false,
                    10)),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        engine.addMapping(
            commandMapping(
                U"<Leader>v",
                "vkery.visual.action",
                metadata(
                    "Visual action",
                    "Visual",
                    "plugin.visual",
                    false,
                    10),
                std::nullopt,
                mappingModes(MappingMode::Visual)),
            &error)
            != 0,
        error.c_str());

    beginLeaderPrefix(engine, MappingMode::Normal);
    auto snapshot =
        engine.pendingPrefixSnapshot(
            MappingMode::Normal,
            std::nullopt,
            1);
    QVERIFY(snapshot.has_value());
    QVERIFY(candidate(*snapshot, U'e') != nullptr);
    QVERIFY(candidate(*snapshot, U'v') == nullptr);

    engine.clear();
    beginLeaderPrefix(engine, MappingMode::Visual);
    snapshot = engine.pendingPrefixSnapshot(
        MappingMode::Visual,
        std::nullopt,
        2);
    QVERIFY(snapshot.has_value());
    QVERIFY(candidate(*snapshot, U'e') == nullptr);
    QVERIFY(candidate(*snapshot, U'v') != nullptr);

    engine.clear();
    const MappingId direct = engine.addMapping(
        commandMapping(
            U"q",
            "vkery.command.direct",
            metadata(
                "Direct command",
                {},
                "plugin.direct",
                false,
                0)),
        &error);
    QVERIFY2(direct != 0, error.c_str());
    engine.appendTyped(characterKey(U'q'));
    const ResolveStep resolved =
        engine.resolve(
            MappingMode::Normal,
            std::nullopt,
            false);
    QVERIFY(resolved.kind == ResolveKind::Command);
    QCOMPARE(
        resolved.commandId,
        std::string("vkery.command.direct"));
    QVERIFY(engine.empty());
}

void VkInputEngineHintsTests::
    triePreservesLongestMatchAndNowait()
{
    std::string error;
    InputEngine longest;
    QVERIFY2(
        longest.addMapping(
            actionMapping(
                U"q",
                HostAction::FocusPanelLeft),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        longest.addMapping(
            actionMapping(
                U"qq",
                HostAction::FocusPanelRight),
            &error)
            != 0,
        error.c_str());
    longest.appendTyped(characterKey(U'q'));
    QVERIFY(
        longest.resolve(
            MappingMode::Normal,
            std::nullopt,
            false)
            .kind
        == ResolveKind::NeedMore);
    const ResolveStep timedOut =
        longest.resolve(
            MappingMode::Normal,
            std::nullopt,
            true);
    QVERIFY(timedOut.kind == ResolveKind::HostAction);
    QVERIFY(
        timedOut.action == HostAction::FocusPanelLeft);

    longest.appendTyped(characterKey(U'q'));
    QVERIFY(
        longest.resolve(
            MappingMode::Normal,
            std::nullopt,
            false)
            .kind
        == ResolveKind::NeedMore);
    longest.appendTyped(characterKey(U'q'));
    const ResolveStep longer =
        longest.resolve(
            MappingMode::Normal,
            std::nullopt,
            false);
    QVERIFY(longer.kind == ResolveKind::HostAction);
    QVERIFY(
        longer.action == HostAction::FocusPanelRight);

    InputEngine nowait;
    QVERIFY2(
        nowait.addMapping(
            actionMapping(
                U"qq",
                HostAction::FocusPanelDown),
            &error)
            != 0,
        error.c_str());
    QVERIFY2(
        nowait.addMapping(
            actionMapping(
                U"q",
                HostAction::FocusPanelUp,
                true),
            &error)
            != 0,
        error.c_str());
    nowait.appendTyped(characterKey(U'q'));
    const ResolveStep immediate =
        nowait.resolve(
            MappingMode::Normal,
            std::nullopt,
            false);
    QVERIFY(immediate.kind == ResolveKind::HostAction);
    QVERIFY(immediate.action == HostAction::FocusPanelUp);

    // A buffer-local longer prefix wins while pending. On timeout the global
    // exact mapping remains the executable fallback, matching the old
    // buffer-local-first resolver semantics.
    InputEngine scoped;
    QVERIFY2(
        scoped.addMapping(
            actionMapping(
                U"q",
                HostAction::FocusPanelLeft),
            &error)
            != 0,
        error.c_str());
    constexpr BufferId buffer = 9;
    QVERIFY2(
        scoped.addMapping(
            actionMapping(
                U"qq",
                HostAction::FocusPanelRight,
                false,
                buffer),
            &error)
            != 0,
        error.c_str());
    scoped.appendTyped(characterKey(U'q'));
    QVERIFY(
        scoped.resolve(
            MappingMode::Normal,
            buffer,
            false)
            .kind
        == ResolveKind::NeedMore);
    const ResolveStep scopedTimeout =
        scoped.resolve(
            MappingMode::Normal,
            buffer,
            true);
    QVERIFY(
        scopedTimeout.kind == ResolveKind::HostAction);
    QVERIFY(
        scopedTimeout.action
        == HostAction::FocusPanelLeft);
}

void VkInputEngineHintsTests::emptyHostActionIsRejected()
{
    InputEngine engine;
    std::string error;
    QCOMPARE(
        engine.addMapping(
            actionMapping(U"x", HostAction::None),
            &error),
        MappingId{0});
    QCOMPARE(
        error,
        std::string(
            "mapping host action target cannot be empty"));
    QVERIFY(engine.empty());
}

void VkInputEngineHintsTests::
    mappingBatchPublishesOnceAndRollsBackExactly()
{
    InputEngine engine;
    std::string error;
    const MappingId original = engine.addMapping(
        commandMapping(
            U"x",
            "test.original",
            metadata("Original", "Test", "test.owner", false, 0)),
        &error);
    QVERIFY2(original != 0, error.c_str());

    const std::vector<MappingDefinition> valid{
        commandMapping(
            U"a",
            "test.first",
            metadata("First", "Test", "test.batch", false, 0)),
        commandMapping(
            U"b",
            "test.second",
            metadata("Second", "Test", "test.batch", false, 0))};
    const std::vector<MappingId> installed =
        engine.addMappings(valid, &error);
    QCOMPARE(installed.size(), std::size_t{2});
    QCOMPARE(installed.front(), original + 1);
    QCOMPARE(installed.back(), original + 2);

    engine.appendTyped(characterKey(U'a'));
    ResolveStep resolved = engine.resolve(
        MappingMode::Normal, std::nullopt, false);
    QCOMPARE(resolved.kind, ResolveKind::Command);
    QCOMPARE(resolved.commandId, std::string("test.first"));
    engine.appendTyped(characterKey(U'b'));
    resolved = engine.resolve(
        MappingMode::Normal, std::nullopt, false);
    QCOMPARE(resolved.kind, ResolveKind::Command);
    QCOMPARE(resolved.commandId, std::string("test.second"));

    MappingDefinition invalid = commandMapping(
        U"z",
        "test.invalid",
        metadata("Invalid", "Test", "test.owner", false, 0));
    invalid.modes = 0;
    const std::vector<MappingDefinition> failedReplacement{
        commandMapping(
            U"x",
            "test.replacement",
            metadata(
                "Replacement", "Test", "test.owner", false, 0)),
        invalid};
    QVERIFY(engine.addMappings(failedReplacement, &error).empty());
    QVERIFY(!error.empty());

    // The first staged member had removed the original in its private copy;
    // failure of the second member must leave the live mapping untouched.
    engine.appendTyped(characterKey(U'x'));
    resolved = engine.resolve(
        MappingMode::Normal, std::nullopt, false);
    QCOMPARE(resolved.kind, ResolveKind::Command);
    QCOMPARE(resolved.commandId, std::string("test.original"));

    const std::vector<MappingDefinition> failedCollision{
        commandMapping(
            U"c",
            "test.partial",
            metadata("Partial", "Test", "test.foreign", false, 0)),
        commandMapping(
            U"x",
            "test.conflict",
            metadata("Conflict", "Test", "test.foreign", false, 0))};
    QVERIFY(engine.addMappings(failedCollision, &error).empty());
    engine.appendTyped(characterKey(U'c'));
    resolved = engine.resolve(
        MappingMode::Normal, std::nullopt, false);
    QCOMPARE(resolved.kind, ResolveKind::EmitKey);

    // Rejected batches do not consume ids.
    const MappingId afterFailures = engine.addMapping(
        commandMapping(
            U"d",
            "test.after",
            metadata("After", "Test", "test.owner", false, 0)),
        &error);
    QCOMPARE(afterFailures, installed.back() + 1);
}

void VkInputEngineHintsTests::
    repeatPrefixCreatesInterruptibleCommandSubmode()
{
    InputEngine engine;
    engine.setLeader(KeySequence{characterKey(U' ')});
    MappingDefinition left = commandMapping(
        U"<Leader>rh",
        "test.resize-left",
        metadata("Resize left", "Resize", "test.resize", false, 0));
    left.options.repeatPrefix = true;
    MappingDefinition right = commandMapping(
        U"<Leader>rl",
        "test.resize-right",
        metadata("Resize right", "Resize", "test.resize", false, 1));
    right.options.repeatPrefix = true;
    std::string error;
    const std::array definitions{left, right};
    const auto ids = engine.addMappings(definitions, &error);
    QCOMPARE(ids.size(), std::size_t{2});
    QVERIFY2(error.empty(), error.c_str());

    engine.appendTyped(characterKey(U' '));
    QCOMPARE(
        engine.resolve(MappingMode::Normal, std::nullopt, false).kind,
        ResolveKind::NeedMore);
    engine.appendTyped(characterKey(U'r'));
    QCOMPARE(
        engine.resolve(MappingMode::Normal, std::nullopt, false).kind,
        ResolveKind::NeedMore);
    engine.appendTyped(characterKey(U'h'));
    ResolveStep resolved = engine.resolve(
        MappingMode::Normal, std::nullopt, false);
    QCOMPARE(resolved.kind, ResolveKind::Command);
    QCOMPARE(resolved.commandId, std::string("test.resize-left"));

    // The complete <Leader>r prefix is now pending without another leader.
    QCOMPARE(
        engine.resolve(MappingMode::Normal, std::nullopt, false).kind,
        ResolveKind::NeedMore);
    engine.appendTyped(characterKey(U'l'));
    resolved = engine.resolve(
        MappingMode::Normal, std::nullopt, false);
    QCOMPARE(resolved.kind, ResolveKind::Command);
    QCOMPARE(resolved.commandId, std::string("test.resize-right"));

    // An unrelated key leaves the submode and is resolved from the root; it
    // is neither swallowed nor preceded by synthetic leader/r literals.
    QCOMPARE(
        engine.resolve(MappingMode::Normal, std::nullopt, false).kind,
        ResolveKind::NeedMore);
    engine.appendTyped(characterKey(U'x'));
    resolved = engine.resolve(
        MappingMode::Normal, std::nullopt, false);
    QCOMPARE(resolved.kind, ResolveKind::EmitKey);
    QCOMPARE(resolved.key, characterKey(U'x'));
    QVERIFY(engine.empty());

    MappingDefinition invalid = commandMapping(
        U"z",
        "test.invalid-repeat",
        metadata("Invalid", "Resize", "test.resize", false, 2));
    invalid.options.repeatPrefix = true;
    QCOMPARE(engine.addMapping(invalid, &error), MappingId{0});
    QCOMPARE(
        error,
        std::string(
            "repeat_prefix requires a command mapping with at least two lhs keys"));
}

void VkInputEngineHintsTests::
    deletedBufferMappingsAreReclaimedAsOneBatch()
{
    InputEngine engine;
    std::string error;
    constexpr BufferId firstRemovedBuffer = 7;
    constexpr BufferId secondRemovedBuffer = 8;
    constexpr BufferId liveBuffer = 9;
    QVERIFY(engine.addMapping(
        actionMapping(U"a", HostAction::FocusPanelLeft),
        &error) != 0);
    QVERIFY(engine.addMapping(
        actionMapping(
            U"a",
            HostAction::FocusPanelRight,
            false,
            firstRemovedBuffer),
        &error) != 0);
    QVERIFY(engine.addMapping(
        actionMapping(
            U"b",
            HostAction::FocusPanelDown,
            false,
            secondRemovedBuffer),
        &error) != 0);
    QVERIFY(engine.addMapping(
        actionMapping(
            U"c",
            HostAction::FocusPanelUp,
            false,
            liveBuffer),
        &error) != 0);

    const std::array removed{
        firstRemovedBuffer,
        secondRemovedBuffer,
        firstRemovedBuffer};
    QCOMPARE(
        engine.removeBufferMappings(removed),
        std::size_t{2});
    engine.appendTyped(characterKey(U'a'));
    ResolveStep result = engine.resolve(
        MappingMode::Normal, firstRemovedBuffer, false);
    QCOMPARE(result.kind, ResolveKind::HostAction);
    QCOMPARE(result.action, HostAction::FocusPanelLeft);
    engine.appendTyped(characterKey(U'b'));
    result = engine.resolve(
        MappingMode::Normal, secondRemovedBuffer, false);
    QCOMPARE(result.kind, ResolveKind::EmitKey);
    engine.appendTyped(characterKey(U'c'));
    result = engine.resolve(
        MappingMode::Normal, liveBuffer, false);
    QCOMPARE(result.kind, ResolveKind::HostAction);
    QCOMPARE(result.action, HostAction::FocusPanelUp);
}

QTEST_GUILESS_MAIN(VkInputEngineHintsTests)

#include "tst_interaction_hints.moc"
