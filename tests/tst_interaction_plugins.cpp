#include <vkui/vk/VkCommandRegistry.h>
#include <vkui/vk/VkCore.h>
#include <vkui/vk/VkCorePluginManager.h>

#include <QKeyEvent>
#include <QTest>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace vkui::vk;

namespace {

[[nodiscard]] PluginSpec commandPlugin(
    std::string id,
    std::string command,
    std::atomic_int *const activationCount = nullptr,
    std::atomic_int *const executionCount = nullptr)
{
    PluginSpec spec;
    spec.id = std::move(id);
    spec.displayName = spec.id;
    spec.version = "1.0.0";
    spec.commands.push_back(
        CommandDeclaration{
            command,
            "Execute " + command});
    spec.activate =
        [command = std::move(command),
         activationCount,
         executionCount](
            ActivationContext &context,
            std::string &error) {
            if (activationCount != nullptr) {
                activationCount->fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
            return context.registerCommand(
                command,
                [executionCount](const CommandInvocation &) {
                    if (executionCount != nullptr) {
                        executionCount->fetch_add(
                            1,
                            std::memory_order_relaxed);
                    }
                    return CommandExecutionResult::success();
                },
                &error);
        };
    return spec;
}

[[nodiscard]] PluginSpec triggerPlugin(
    std::string id,
    PluginActivationTrigger trigger,
    std::atomic_int *const activationCount = nullptr)
{
    PluginSpec spec;
    spec.id = std::move(id);
    spec.displayName = spec.id;
    spec.version = "1.0.0";
    spec.activationTriggers.push_back(std::move(trigger));
    spec.activate =
        [activationCount](
            ActivationContext &,
            std::string &) {
            if (activationCount != nullptr) {
                activationCount->fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
            return true;
        };
    return spec;
}

[[nodiscard]] bool containsText(
    const std::string &value,
    const std::string_view expected)
{
    return value.find(expected) != std::string::npos;
}

} // namespace

class VkCorePluginManagerTests final : public QObject
{
    Q_OBJECT

private slots:
    void contributionLeaseRevokesOnlyItsExactRegistration();
    void dependencyClosureActivatesInDeterministicOrder();
    void concurrentLazyCommandActivatesPluginOnce();
    void missingDependencyAndCycleAreRejected();
    void disablingPluginIsOwnerIsolatedAndDependencySafe();
    void cleanupIsOwnerIsolatedAndRunsInReverseOrder();
    void cleanupFailureStillCommitsDisabledState();
    void failedActivationRollsBackEveryContribution();
    void failedEagerPluginCanRetryEnable();
    void startupActivationLoadsOnlyEagerClosure();
    void typedTriggersMatchOnlyResolvedSemanticValues();
    void triggeredDependencyOrderAndFailuresAreIsolated();
    void disabledTriggeredPluginsRemainDormant();
    void concurrentTriggerNotificationActivatesExactlyOnce();
    void mappingsCommitAndRevokeWithTheirPluginOwner();
    void userCommandsCommitRollbackAndRevokeWithTheirOwner();
    void concurrentActivationAndDisableArePublicationAtomic();
    void cleanupCanReenterAndOldGenerationCannotRevokeNew();
};

void VkCorePluginManagerTests::
    contributionLeaseRevokesOnlyItsExactRegistration()
{
    static_assert(
        !std::is_copy_constructible_v<
            VkCommandRegistry::ContributionLease>);
    static_assert(
        std::is_nothrow_move_constructible_v<
            VkCommandRegistry::ContributionLease>);

    VkCommandRegistry registry;
    auto first = registry.registerCommand(
        "plugin.alpha",
        CommandDescriptor{
            "vkery.alpha.run",
            "Run alpha"},
        [](const CommandInvocation &) {
            return CommandExecutionResult::success();
        });
    QVERIFY(first.valid());
    QVERIFY(registry.contains("vkery.alpha.run"));

    auto moved = std::move(first);
    QVERIFY(!first.valid());
    QVERIFY(moved.valid());
    moved.reset();
    QVERIFY(!registry.contains("vkery.alpha.run"));

    auto stale = registry.registerCommand(
        "plugin.alpha",
        CommandDescriptor{
            "vkery.shared.run",
            "Old owner"},
        [](const CommandInvocation &) {
            return CommandExecutionResult::success();
        });
    QVERIFY(stale.valid());
    QCOMPARE(
        registry.revokeOwner("plugin.alpha"),
        std::size_t{1});
    QVERIFY(!stale.valid());

    auto replacement = registry.registerCommand(
        "plugin.beta",
        CommandDescriptor{
            "vkery.shared.run",
            "New owner"},
        [](const CommandInvocation &) {
            return CommandExecutionResult::success();
        });
    QVERIFY(replacement.valid());
    stale.reset();
    QVERIFY(registry.contains("vkery.shared.run"));
    QCOMPARE(
        registry.ownerOf("vkery.shared.run").value(),
        std::string("plugin.beta"));

    std::string error;
    std::vector<VkCommandRegistry::CommandRegistration>
        conflictingBatch;
    conflictingBatch.push_back(
        VkCommandRegistry::CommandRegistration{
            CommandDescriptor{
                "vkery.fresh.run",
                "Fresh"},
            [](const CommandInvocation &) {
                return CommandExecutionResult::success();
            }});
    conflictingBatch.push_back(
        VkCommandRegistry::CommandRegistration{
            CommandDescriptor{
                "vkery.shared.run",
                "Conflict"},
            [](const CommandInvocation &) {
                return CommandExecutionResult::success();
            }});
    const auto rejected = registry.registerCommands(
        "plugin.gamma",
        std::move(conflictingBatch),
        &error);
    QVERIFY(rejected.empty());
    QVERIFY2(
        containsText(error, "already owned"),
        error.c_str());
    QVERIFY(!registry.contains("vkery.fresh.run"));
    QVERIFY(registry.contains("vkery.shared.run"));
}

void VkCorePluginManagerTests::
    dependencyClosureActivatesInDeterministicOrder()
{
    VkCorePluginManager manager;
    std::vector<std::string> activationOrder;

    PluginSpec low;
    low.id = "plugin.low";
    low.displayName = "Low";
    low.version = "1.0.0";
    low.priority = 10;
    low.activate =
        [&activationOrder](
            ActivationContext &,
            std::string &) {
            activationOrder.emplace_back("low");
            return true;
        };

    PluginSpec high;
    high.id = "plugin.high";
    high.displayName = "High";
    high.version = "1.0.0";
    high.priority = 50;
    high.activate =
        [&activationOrder](
            ActivationContext &,
            std::string &) {
            activationOrder.emplace_back("high");
            return true;
        };

    PluginSpec root =
        commandPlugin(
            "plugin.root",
            "vkery.root.run");
    root.dependencies = {
        "plugin.low",
        "plugin.high"};
    const auto originalActivation =
        std::move(root.activate);
    root.activate =
        [&activationOrder,
         originalActivation](
            ActivationContext &context,
            std::string &error) {
            activationOrder.emplace_back("root");
            return originalActivation(context, error);
        };

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(low), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(root), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(high), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());

    const std::vector<PluginId> resolvedOrder =
        manager.topologicalOrder();
    QCOMPARE(
        resolvedOrder,
        std::vector<PluginId>({
            "plugin.high",
            "plugin.low",
            "plugin.root"}));

    const CommandExecutionResult executed =
        manager.executeCommand("vkery.root.run");
    QVERIFY2(executed.succeeded, executed.error.c_str());
    QCOMPARE(
        activationOrder,
        std::vector<std::string>({
            "high",
            "low",
            "root"}));

    const auto highInfo =
        manager.pluginInfo("plugin.high");
    const auto rootInfo =
        manager.pluginInfo("plugin.root");
    QVERIFY(highInfo.has_value());
    QVERIFY(rootInfo.has_value());
    QCOMPARE(
        highInfo->activationReason,
        std::string("dependency:plugin.root"));
    QCOMPARE(
        rootInfo->activationReason,
        std::string("command:vkery.root.run"));
}

void VkCorePluginManagerTests::
    concurrentLazyCommandActivatesPluginOnce()
{
    VkCorePluginManager manager;
    std::atomic_int activations = 0;
    std::atomic_int executions = 0;
    PluginSpec spec = commandPlugin(
        "plugin.lazy",
        "vkery.lazy.run",
        &activations,
        &executions);
    const auto activation = std::move(spec.activate);
    spec.activate =
        [activation](
            ActivationContext &context,
            std::string &error) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
            return activation(context, error);
        };

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(spec), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());
    QVERIFY(
        !manager.commandRegistry().contains(
            "vkery.lazy.run"));

    constexpr std::size_t threadCount = 8;
    std::vector<CommandExecutionResult> results(threadCount);
    {
        std::vector<std::jthread> threads;
        threads.reserve(threadCount);
        for (std::size_t index = 0;
             index < threadCount;
             ++index) {
            threads.emplace_back(
                [&manager, &results, index] {
                    results[index] =
                        manager.executeCommand(
                            "vkery.lazy.run");
                });
        }
    }

    for (const CommandExecutionResult &result : results) {
        QVERIFY2(result.succeeded, result.error.c_str());
    }
    QCOMPARE(activations.load(), 1);
    QCOMPARE(
        executions.load(),
        static_cast<int>(threadCount));

    const auto info = manager.pluginInfo("plugin.lazy");
    QVERIFY(info.has_value());
    QVERIFY(info->state == PluginState::Ready);
    QCOMPARE(info->activationAttempts, std::size_t{1});
    QCOMPARE(info->successfulActivations, std::size_t{1});
}

void VkCorePluginManagerTests::
    missingDependencyAndCycleAreRejected()
{
    std::string error;

    VkCorePluginManager missing;
    PluginSpec orphan;
    orphan.id = "plugin.orphan";
    orphan.displayName = "Orphan";
    orphan.version = "1.0.0";
    orphan.dependencies = {"plugin.absent"};
    QVERIFY2(
        missing.addPlugin(std::move(orphan), &error),
        error.c_str());
    QVERIFY(!missing.resolve(&error));
    QVERIFY2(
        containsText(error, "missing dependency"),
        error.c_str());
    QVERIFY2(
        containsText(error, "plugin.absent"),
        error.c_str());

    VkCorePluginManager cycle;
    PluginSpec first;
    first.id = "plugin.first";
    first.displayName = "First";
    first.version = "1.0.0";
    first.dependencies = {"plugin.second"};
    PluginSpec second;
    second.id = "plugin.second";
    second.displayName = "Second";
    second.version = "1.0.0";
    second.dependencies = {"plugin.first"};
    QVERIFY2(
        cycle.addPlugin(std::move(first), &error),
        error.c_str());
    QVERIFY2(
        cycle.addPlugin(std::move(second), &error),
        error.c_str());
    QVERIFY(!cycle.resolve(&error));
    QVERIFY2(
        containsText(error, "dependency cycle"),
        error.c_str());
    QVERIFY2(
        containsText(error, "plugin.first"),
        error.c_str());
    QVERIFY2(
        containsText(error, "plugin.second"),
        error.c_str());
}

void VkCorePluginManagerTests::
    disablingPluginIsOwnerIsolatedAndDependencySafe()
{
    std::string error;
    VkCorePluginManager independent;
    QVERIFY2(
        independent.addPlugin(
            commandPlugin(
                "plugin.alpha",
                "vkery.alpha.run"),
            &error),
        error.c_str());
    QVERIFY2(
        independent.addPlugin(
            commandPlugin(
                "plugin.beta",
                "vkery.beta.run"),
            &error),
        error.c_str());
    QVERIFY2(independent.resolve(&error), error.c_str());
    QVERIFY(
        independent.executeCommand(
            "vkery.alpha.run"));
    QVERIFY(
        independent.executeCommand(
            "vkery.beta.run"));

    QVERIFY2(
        independent.disablePlugin(
            "plugin.alpha",
            &error),
        error.c_str());
    QVERIFY(
        !independent.commandRegistry().contains(
            "vkery.alpha.run"));
    QVERIFY(
        independent.commandRegistry().contains(
            "vkery.beta.run"));
    const CommandExecutionResult disabled =
        independent.executeCommand("vkery.alpha.run");
    QVERIFY(!disabled);
    QVERIFY2(
        containsText(disabled.error, "disabled"),
        disabled.error.c_str());
    QVERIFY(
        independent.executeCommand(
            "vkery.beta.run"));

    VkCorePluginManager dependent;
    PluginSpec base;
    base.id = "plugin.base";
    base.displayName = "Base";
    base.version = "1.0.0";
    PluginSpec child;
    child.id = "plugin.child";
    child.displayName = "Child";
    child.version = "1.0.0";
    child.dependencies = {"plugin.base"};
    QVERIFY2(
        dependent.addPlugin(std::move(base), &error),
        error.c_str());
    QVERIFY2(
        dependent.addPlugin(std::move(child), &error),
        error.c_str());
    QVERIFY2(dependent.resolve(&error), error.c_str());
    QVERIFY(
        !dependent.disablePlugin(
            "plugin.base",
            &error));
    QVERIFY2(
        containsText(error, "plugin.child"),
        error.c_str());
    const auto baseInfo =
        dependent.pluginInfo("plugin.base");
    QVERIFY(baseInfo.has_value());
    QVERIFY(baseInfo->state == PluginState::Resolved);
    QVERIFY(baseInfo->enabled);
}

void VkCorePluginManagerTests::
    cleanupIsOwnerIsolatedAndRunsInReverseOrder()
{
    int alphaSideEffect = 0;
    int betaSideEffect = 0;
    std::vector<std::string> cleanupOrder;
    VkCorePluginManager manager;

    PluginSpec alpha = commandPlugin(
        "plugin.alpha",
        "vkery.alpha.run");
    const auto activateAlpha = std::move(alpha.activate);
    alpha.activate =
        [&alphaSideEffect,
         &cleanupOrder,
         activateAlpha](
            ActivationContext &context,
            std::string &error) {
            alphaSideEffect = 1;
            if (!context.addCleanup(
                    [&alphaSideEffect, &cleanupOrder] {
                        cleanupOrder.emplace_back(
                            "alpha-first");
                        alphaSideEffect = 0;
                    },
                    &error)) {
                return false;
            }
            if (!context.addCleanup(
                    [&cleanupOrder] {
                        cleanupOrder.emplace_back(
                            "alpha-second");
                    },
                    &error)) {
                return false;
            }
            return activateAlpha(context, error);
        };

    PluginSpec beta = commandPlugin(
        "plugin.beta",
        "vkery.beta.run");
    const auto activateBeta = std::move(beta.activate);
    beta.activate =
        [&betaSideEffect,
         &cleanupOrder,
         activateBeta](
            ActivationContext &context,
            std::string &error) {
            betaSideEffect = 1;
            if (!context.addCleanup(
                    [&betaSideEffect, &cleanupOrder] {
                        cleanupOrder.emplace_back("beta");
                        betaSideEffect = 0;
                    },
                    &error)) {
                return false;
            }
            return activateBeta(context, error);
        };

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(alpha), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(beta), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());
    QVERIFY(manager.executeCommand("vkery.alpha.run"));
    QVERIFY(manager.executeCommand("vkery.beta.run"));
    QCOMPARE(alphaSideEffect, 1);
    QCOMPARE(betaSideEffect, 1);

    QVERIFY2(
        manager.disablePlugin("plugin.alpha", &error),
        error.c_str());
    QCOMPARE(alphaSideEffect, 0);
    QCOMPARE(betaSideEffect, 1);
    QCOMPARE(
        cleanupOrder,
        std::vector<std::string>({
            "alpha-second",
            "alpha-first"}));
    QVERIFY(
        manager.commandRegistry().contains(
            "vkery.beta.run"));
    QVERIFY(manager.executeCommand("vkery.beta.run"));
    QVERIFY2(
        manager.disablePlugin("plugin.beta", &error),
        error.c_str());
    QCOMPARE(betaSideEffect, 0);
    QCOMPARE(cleanupOrder.back(), std::string("beta"));
}

void VkCorePluginManagerTests::
    cleanupFailureStillCommitsDisabledState()
{
    VkCorePluginManager manager;
    PluginSpec plugin = commandPlugin(
        "plugin.cleanup-failure",
        "vkery.cleanup-failure.run");
    const auto activateCommand = std::move(plugin.activate);
    plugin.activate =
        [activateCommand](
            ActivationContext &context,
            std::string &error) {
            if (!context.addCleanup(
                    [] {
                        throw std::runtime_error(
                            "simulated cleanup failure");
                    },
                    &error)) {
                return false;
            }
            return activateCommand(context, error);
        };

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(plugin), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());
    QVERIFY(manager.executeCommand(
        "vkery.cleanup-failure.run"));

    // Command revocation and the disabled state cannot be rolled back after
    // cleanup begins, so the API reports the committed transition as success.
    QVERIFY2(
        manager.disablePlugin(
            "plugin.cleanup-failure", &error),
        error.c_str());
    QVERIFY(error.empty());
    QVERIFY(!manager.commandRegistry().contains(
        "vkery.cleanup-failure.run"));
    const auto info =
        manager.pluginInfo("plugin.cleanup-failure");
    QVERIFY(info.has_value());
    QVERIFY(!info->enabled);
    QCOMPARE(info->state, PluginState::Disabled);
    QVERIFY2(
        containsText(
            info->lastError,
            "cleanup callback(s) failed"),
        info->lastError.c_str());
}

void VkCorePluginManagerTests::
    failedActivationRollsBackEveryContribution()
{
    VkCorePluginManager manager;
    int sideEffect = 0;
    PluginSpec failing;
    failing.id = "plugin.failing";
    failing.displayName = "Failing";
    failing.version = "1.0.0";
    failing.commands.push_back(
        CommandDeclaration{
            "vkery.failing.run",
            "Fail"});
    failing.activate =
        [&sideEffect](
            ActivationContext &context,
            std::string &error) {
            sideEffect = 1;
            if (!context.addCleanup(
                    [&sideEffect] {
                        sideEffect = 0;
                    },
                    &error)) {
                return false;
            }
            if (!context.registerCommand(
                    "vkery.failing.run",
                    [](const CommandInvocation &) {
                        return CommandExecutionResult::success();
                    },
                    &error)) {
                return false;
            }
            error = "simulated activation failure";
            return false;
        };

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(failing), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());
    const CommandExecutionResult result =
        manager.executeCommand("vkery.failing.run");
    QVERIFY(!result);
    QVERIFY2(
        containsText(
            result.error,
            "simulated activation failure"),
        result.error.c_str());
    QVERIFY(
        !manager.commandRegistry().contains(
            "vkery.failing.run"));
    QCOMPARE(sideEffect, 0);
    QCOMPARE(
        manager.commandRegistry().size(),
        std::size_t{0});

    const auto info =
        manager.pluginInfo("plugin.failing");
    QVERIFY(info.has_value());
    QVERIFY(info->state == PluginState::Failed);
    QCOMPARE(info->activationAttempts, std::size_t{1});
    QCOMPARE(info->successfulActivations, std::size_t{0});

    const CommandExecutionResult second =
        manager.executeCommand("vkery.failing.run");
    QVERIFY(!second);
    QCOMPARE(
        manager.pluginInfo("plugin.failing")
            ->activationAttempts,
        std::size_t{1});
}

void VkCorePluginManagerTests::
    failedEagerPluginCanRetryEnable()
{
    VkCorePluginManager manager;
    int attempts = 0;
    PluginSpec plugin;
    plugin.id = "plugin.retry";
    plugin.displayName = "Retry";
    plugin.version = "1.0.0";
    plugin.lazy = false;
    plugin.commands.push_back(
        CommandDeclaration{
            "vkery.retry.run",
            "Retry command"});
    plugin.activate =
        [&attempts](
            ActivationContext &context,
            std::string &error) {
            ++attempts;
            if (attempts == 1) {
                error = "first activation failed";
                return false;
            }
            return context.registerCommand(
                "vkery.retry.run",
                [](const CommandInvocation &) {
                    return CommandExecutionResult::success();
                },
                &error);
        };

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(plugin), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());
    QVERIFY(!manager.activateStartupPlugins(&error));
    QCOMPARE(attempts, 1);
    const auto failed = manager.pluginInfo("plugin.retry");
    QVERIFY(failed.has_value());
    QVERIFY(failed->enabled);
    QCOMPARE(failed->state, PluginState::Failed);

    QVERIFY2(
        manager.enablePlugin("plugin.retry", &error),
        error.c_str());
    QCOMPARE(attempts, 2);
    const auto ready = manager.pluginInfo("plugin.retry");
    QVERIFY(ready.has_value());
    QVERIFY(ready->enabled);
    QCOMPARE(ready->state, PluginState::Ready);
    QCOMPARE(ready->activationAttempts, std::size_t{2});
    QCOMPARE(ready->successfulActivations, std::size_t{1});
    QVERIFY(manager.executeCommand("vkery.retry.run"));
}

void VkCorePluginManagerTests::
    startupActivationLoadsOnlyEagerClosure()
{
    VkCorePluginManager manager;
    std::vector<std::string> activationOrder;

    PluginSpec dependency;
    dependency.id = "plugin.lazy-dependency";
    dependency.displayName = "Lazy dependency";
    dependency.version = "1.0.0";
    dependency.lazy = true;
    dependency.activate =
        [&activationOrder](
            ActivationContext &,
            std::string &) {
            activationOrder.emplace_back("dependency");
            return true;
        };

    PluginSpec eager;
    eager.id = "plugin.eager";
    eager.displayName = "Eager";
    eager.version = "1.0.0";
    eager.lazy = false;
    eager.dependencies = {"plugin.lazy-dependency"};
    eager.activate =
        [&activationOrder](
            ActivationContext &,
            std::string &) {
            activationOrder.emplace_back("eager");
            return true;
        };

    PluginSpec untouched;
    untouched.id = "plugin.untouched";
    untouched.displayName = "Untouched";
    untouched.version = "1.0.0";
    untouched.lazy = true;
    untouched.activate =
        [&activationOrder](
            ActivationContext &,
            std::string &) {
            activationOrder.emplace_back("untouched");
            return true;
        };

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(eager), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(untouched), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(dependency), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());
    QVERIFY2(
        manager.activateStartupPlugins(&error),
        error.c_str());

    QCOMPARE(
        activationOrder,
        std::vector<std::string>({
            "dependency",
            "eager"}));
    QVERIFY(
        manager.pluginInfo("plugin.lazy-dependency")
            ->state
        == PluginState::Ready);
    QVERIFY(
        manager.pluginInfo("plugin.eager")->state
        == PluginState::Ready);
    QVERIFY(
        manager.pluginInfo("plugin.untouched")->state
        == PluginState::Resolved);
}

void VkCorePluginManagerTests::
    typedTriggersMatchOnlyResolvedSemanticValues()
{
    {
        VkCorePluginManager invalidManager;
        PluginSpec invalid = triggerPlugin(
            "plugin.invalid-prefix",
            MappingPrefixTrigger{
                std::u32string{},
                mappingModes(MappingMode::Normal)});
        std::string validationError;
        QVERIFY(
            !invalidManager.addPlugin(
                std::move(invalid),
                &validationError));
        QVERIFY2(
            containsText(
                validationError,
                "exact match must not be empty"),
            validationError.c_str());
    }

    VkCorePluginManager manager;
    std::atomic_int prefixActivations = 0;
    std::atomic_int eventActivations = 0;
    std::atomic_int bufferActivations = 0;
    std::atomic_int windowActivations = 0;
    std::atomic_int anyPrefixActivations = 0;

    PluginSpec prefix = triggerPlugin(
        "plugin.prefix",
        MappingPrefixTrigger{
            U"<Leader>",
            MappingMode::Normal | MappingMode::Visual},
        &prefixActivations);
    PluginSpec event = triggerPlugin(
        "plugin.event",
        CoreEventTrigger{
            EventType::BufferActivationRequested},
        &eventActivations);
    PluginSpec buffer = triggerPlugin(
        "plugin.buffer",
        BufferKindTrigger{BufferKind::Reader},
        &bufferActivations);
    PluginSpec window = triggerPlugin(
        "plugin.window",
        WindowKindTrigger{WindowKind::Navigation},
        &windowActivations);
    PluginSpec anyPrefix = triggerPlugin(
        "plugin.any-prefix",
        MappingPrefixTrigger{
            std::nullopt,
            mappingModes(MappingMode::Normal)},
        &anyPrefixActivations);

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(anyPrefix), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(window), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(buffer), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(event), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(prefix), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());

    const TriggerActivationReport wrongPrefix =
        manager.notify(
            MappingPrefixNotification{
                MappingMode::Insert,
                U"<Leader>",
                std::nullopt});
    QVERIFY(wrongPrefix);
    QVERIFY(wrongPrefix.matchedPlugins.empty());
    QCOMPARE(prefixActivations.load(), 0);
    QCOMPARE(anyPrefixActivations.load(), 0);

    // A wildcard declaration observes the resolved prefix. It therefore
    // follows a user-configured <Space> leader without declaring that key.
    const TriggerActivationReport configuredLeader =
        manager.notify(
            MappingPrefixNotification{
                MappingMode::Normal,
                U"<Space>",
                std::nullopt});
    QVERIFY(configuredLeader);
    QCOMPARE(
        configuredLeader.matchedPlugins,
        std::vector<PluginId>({"plugin.any-prefix"}));
    QCOMPARE(anyPrefixActivations.load(), 1);

    const TriggerActivationReport prefixReport =
        manager.activateTriggered(
            MappingPrefixNotification{
                MappingMode::Normal,
                U"<Leader>",
                BufferId{17}});
    QVERIFY(prefixReport);
    QCOMPARE(
        prefixReport.matchedPlugins,
        std::vector<PluginId>({
            "plugin.any-prefix",
            "plugin.prefix"}));
    QCOMPARE(
        prefixReport.readyPlugins,
        prefixReport.matchedPlugins);
    QCOMPARE(prefixActivations.load(), 1);
    QCOMPARE(anyPrefixActivations.load(), 1);

    const TriggerActivationReport unrelatedEvent =
        manager.notify(
            CoreEventNotification{
                EventType::ViewportChanged});
    QVERIFY(unrelatedEvent);
    QVERIFY(unrelatedEvent.matchedPlugins.empty());
    QCOMPARE(eventActivations.load(), 0);

    QVERIFY(
        manager.notify(
            CoreEventNotification{
                EventType::BufferActivationRequested}));
    QVERIFY(
        manager.notify(
            BufferKindNotification{
                BufferId{42},
                BufferKind::Reader}));
    QVERIFY(
        manager.notify(
            WindowKindNotification{
                WindowId{9},
                WindowKind::Navigation}));

    QCOMPARE(eventActivations.load(), 1);
    QCOMPARE(bufferActivations.load(), 1);
    QCOMPARE(windowActivations.load(), 1);
    QCOMPARE(
        manager.pluginInfo("plugin.prefix")
            ->activationReason,
        std::string("trigger:mapping-prefix"));
    QCOMPARE(
        manager.pluginInfo("plugin.event")
            ->activationReason,
        std::string("trigger:event"));
}

void VkCorePluginManagerTests::
    triggeredDependencyOrderAndFailuresAreIsolated()
{
    VkCorePluginManager manager;
    std::vector<std::string> activationOrder;

    PluginSpec dependency;
    dependency.id = "plugin.dependency";
    dependency.displayName = "Dependency";
    dependency.version = "1.0.0";
    dependency.priority = 100;
    dependency.activate =
        [&activationOrder](
            ActivationContext &,
            std::string &) {
            activationOrder.emplace_back("dependency");
            return true;
        };

    PluginSpec high = triggerPlugin(
        "plugin.high",
        CoreEventTrigger{EventType::ModeChanged});
    high.priority = 50;
    high.dependencies = {"plugin.dependency"};
    high.activate =
        [&activationOrder](
            ActivationContext &,
            std::string &) {
            activationOrder.emplace_back("high");
            return true;
        };

    PluginSpec failing = triggerPlugin(
        "plugin.failing-trigger",
        CoreEventTrigger{EventType::ModeChanged});
    failing.priority = 40;
    failing.activate =
        [&activationOrder](
            ActivationContext &,
            std::string &error) {
            activationOrder.emplace_back("failing");
            error = "isolated trigger failure";
            return false;
        };

    PluginSpec low = triggerPlugin(
        "plugin.low",
        CoreEventTrigger{EventType::ModeChanged});
    low.priority = 10;
    low.activate =
        [&activationOrder](
            ActivationContext &,
            std::string &) {
            activationOrder.emplace_back("low");
            return true;
        };

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(low), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(failing), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(high), &error),
        error.c_str());
    QVERIFY2(
        manager.addPlugin(std::move(dependency), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());

    const TriggerActivationReport report =
        manager.activateTriggered(
            CoreEventNotification{
                EventType::ModeChanged});
    QVERIFY(!report);
    QCOMPARE(
        report.matchedPlugins,
        std::vector<PluginId>({
            "plugin.high",
            "plugin.failing-trigger",
            "plugin.low"}));
    QCOMPARE(
        report.readyPlugins,
        std::vector<PluginId>({
            "plugin.high",
            "plugin.low"}));
    QCOMPARE(report.failures.size(), std::size_t{1});
    QCOMPARE(
        report.failures.front().plugin,
        std::string("plugin.failing-trigger"));
    QVERIFY2(
        containsText(
            report.failures.front().error,
            "isolated trigger failure"),
        report.failures.front().error.c_str());
    QCOMPARE(
        activationOrder,
        std::vector<std::string>({
            "dependency",
            "high",
            "failing",
            "low"}));
    QVERIFY(
        manager.pluginInfo("plugin.high")->state
        == PluginState::Ready);
    QVERIFY(
        manager.pluginInfo("plugin.low")->state
        == PluginState::Ready);
    QVERIFY(
        manager.pluginInfo("plugin.failing-trigger")->state
        == PluginState::Failed);
}

void VkCorePluginManagerTests::
    disabledTriggeredPluginsRemainDormant()
{
    VkCorePluginManager manager;
    std::atomic_int activations = 0;
    PluginSpec plugin = triggerPlugin(
        "plugin.optional",
        BufferKindTrigger{BufferKind::Text},
        &activations);
    plugin.defaultEnabled = false;

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(plugin), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());

    const PluginActivationNotification notification =
        BufferKindNotification{
            BufferId{1},
            BufferKind::Text};
    const TriggerActivationReport disabled =
        manager.notify(notification);
    QVERIFY(disabled);
    QVERIFY(disabled.matchedPlugins.empty());
    QCOMPARE(activations.load(), 0);

    QVERIFY2(
        manager.enablePlugin("plugin.optional", &error),
        error.c_str());
    const TriggerActivationReport enabled =
        manager.notify(notification);
    QVERIFY(enabled);
    QCOMPARE(
        enabled.readyPlugins,
        std::vector<PluginId>({"plugin.optional"}));
    QCOMPARE(activations.load(), 1);

    QVERIFY2(
        manager.disablePlugin("plugin.optional", &error),
        error.c_str());
    const TriggerActivationReport disabledAgain =
        manager.notify(notification);
    QVERIFY(disabledAgain);
    QVERIFY(disabledAgain.matchedPlugins.empty());
    QCOMPARE(activations.load(), 1);
}

void VkCorePluginManagerTests::
    concurrentTriggerNotificationActivatesExactlyOnce()
{
    VkCorePluginManager manager;
    std::atomic_int activations = 0;
    PluginSpec plugin = triggerPlugin(
        "plugin.concurrent-trigger",
        WindowKindTrigger{WindowKind::Editor});
    plugin.activate =
        [&activations](
            ActivationContext &,
            std::string &) {
            activations.fetch_add(
                1,
                std::memory_order_relaxed);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
            return true;
        };

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(plugin), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());

    constexpr std::size_t threadCount = 8;
    std::vector<TriggerActivationReport> reports(threadCount);
    {
        std::vector<std::jthread> threads;
        threads.reserve(threadCount);
        for (std::size_t index = 0;
             index < threadCount;
             ++index) {
            threads.emplace_back(
                [&manager, &reports, index] {
                    reports[index] =
                        manager.notify(
                            WindowKindNotification{
                                WindowId{index + 1},
                                WindowKind::Editor});
                });
        }
    }

    for (const TriggerActivationReport &report : reports) {
        QVERIFY(report);
        QCOMPARE(
            report.matchedPlugins,
            std::vector<PluginId>({
                "plugin.concurrent-trigger"}));
        QCOMPARE(report.readyPlugins, report.matchedPlugins);
    }
    QCOMPARE(activations.load(), 1);
    QCOMPARE(
        manager.pluginInfo("plugin.concurrent-trigger")
            ->activationAttempts,
        std::size_t{1});
}

void VkCorePluginManagerTests::
    mappingsCommitAndRevokeWithTheirPluginOwner()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Navigation);
    VkCorePluginManager manager;
    std::optional<CommandInvocation> received;

    PluginSpec plugin;
    plugin.id = "plugin.keymap-owner";
    plugin.displayName = "Keymap owner";
    plugin.version = "1.0.0";
    plugin.lazy = false;
    plugin.commands = {
        {"plugin.keymap-owner.run", "Run mapping"}};
    plugin.activate =
        [&core, window, &received](
            ActivationContext &context,
            std::string &error) {
            if (!context.registerCommand(
                    "plugin.keymap-owner.run",
                    [&received](
                        const CommandInvocation &invocation) {
                        received = invocation;
                        return CommandExecutionResult::success();
                    },
                    &error)) {
                return false;
            }
            MappingDefinition mapping;
            mapping.modes =
                mappingModes(MappingMode::Normal);
            mapping.lhs = U"x";
            mapping.target = CommandTarget{
                "plugin.keymap-owner.run"};
            mapping.window = window;
            mapping.metadata.description = "Run mapping";
            if (!context.registerMapping(
                    core, mapping, &error)) {
                return false;
            }
            mapping.lhs = U"dz";
            mapping.metadata.description =
                "Wait before running mapping";
            return context.registerMapping(
                core, std::move(mapping), &error);
        };

    std::string error;
    QVERIFY2(
        manager.addPlugin(std::move(plugin), &error),
        error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());
    QVERIFY2(
        manager.activateStartupPlugins(&error),
        error.c_str());

    const QKeyEvent count(
        QEvent::KeyPress,
        Qt::Key_3,
        Qt::NoModifier,
        QStringLiteral("3"));
    (void)core.dispatch(window, 77, count);
    const QKeyEvent execute(
        QEvent::KeyPress,
        Qt::Key_X,
        Qt::NoModifier,
        QStringLiteral("x"));
    const DispatchResult dispatch =
        core.dispatch(window, 77, execute);
    const auto requested = std::ranges::find_if(
        dispatch.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        });
    QVERIFY(requested != dispatch.events.cend());
    QCOMPARE(requested->count, std::size_t{3});
    QVERIFY(requested->countWasExplicit);
    QCOMPARE(requested->view, window);
    QCOMPARE(requested->inputTarget, InputTargetId{77});
    QVERIFY(manager.executeCommand(
        CommandInvocation{
            requested->commandId,
            {},
            requested->count,
            requested->countWasExplicit,
            requested->mode,
            requested->view,
            requested->buffer,
            requested->inputTarget}));
    QVERIFY(received.has_value());
    QCOMPARE(received->count, std::size_t{3});
    QCOMPARE(received->window, window);

    const QKeyEvent pendingKey(
        QEvent::KeyPress,
        Qt::Key_D,
        Qt::NoModifier,
        QStringLiteral("d"));
    const DispatchResult pending =
        core.dispatch(window, 77, pendingKey);
    QCOMPARE(pending.disposition, InputDisposition::Pending);
    QVERIFY(pending.inputHintGeneration.has_value());
    const std::uint64_t staleGeneration =
        *pending.inputHintGeneration;

    QVERIFY2(
        manager.disablePlugin("plugin.keymap-owner", &error),
        error.c_str());
    QVERIFY(!core.pendingInputHint(staleGeneration));
    QVERIFY(core.mappingTimedOut(staleGeneration).events.empty());
    const DispatchResult afterDisable =
        core.dispatch(window, 77, execute);
    QVERIFY(std::ranges::none_of(
        afterDisable.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        }));

    VkCore rollbackCore;
    const WindowId rollbackWindow =
        rollbackCore.registerWindow(WindowKind::Navigation);
    VkCorePluginManager rollbackManager;
    PluginSpec broken;
    broken.id = "plugin.broken-keymaps";
    broken.displayName = "Broken keymaps";
    broken.version = "1.0.0";
    broken.lazy = false;
    broken.commands = {
        {"plugin.broken-keymaps.run", "Run"}};
    broken.activate =
        [&rollbackCore](
            ActivationContext &context,
            std::string &activationError) {
            if (!context.registerCommand(
                    "plugin.broken-keymaps.run",
                    [](const CommandInvocation &) {
                        return CommandExecutionResult::success();
                    },
                    &activationError)) {
                return false;
            }
            MappingDefinition first;
            first.modes = mappingModes(MappingMode::Normal);
            first.lhs = U"x";
            first.target = CommandTarget{
                "plugin.broken-keymaps.run"};
            if (!context.registerMapping(
                    rollbackCore,
                    std::move(first),
                    &activationError)) {
                return false;
            }
            MappingDefinition invalid;
            invalid.modes = mappingModes(MappingMode::Normal);
            invalid.lhs = U"y";
            invalid.target = CommandTarget{
                "plugin.broken-keymaps.run"};
            invalid.window = WindowId{999'999};
            return context.registerMapping(
                rollbackCore,
                std::move(invalid),
                &activationError);
        };
    QVERIFY2(
        rollbackManager.addPlugin(
            std::move(broken), &error),
        error.c_str());
    QVERIFY2(
        rollbackManager.resolve(&error),
        error.c_str());
    QVERIFY(!rollbackManager.activateStartupPlugins(&error));
    QVERIFY(error.find("live window") != std::string::npos);
    QVERIFY(!rollbackManager.commandRegistry().contains(
        "plugin.broken-keymaps.run"));
    const DispatchResult afterRollback =
        rollbackCore.dispatch(
            rollbackWindow, 0, execute);
    QVERIFY(std::ranges::none_of(
        afterRollback.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        }));
    MappingDefinition afterFailedBatch;
    afterFailedBatch.modes =
        mappingModes(MappingMode::Normal);
    afterFailedBatch.lhs = U"q";
    afterFailedBatch.target =
        HostActionTarget{HostAction::FocusPanelLeft};
    QCOMPARE(
        rollbackCore.addMapping(
            afterFailedBatch, &error),
        MappingId{1});
}

void VkCorePluginManagerTests::
    userCommandsCommitRollbackAndRevokeWithTheirOwner()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        window,
        "/vault/user-command.txt",
        u"body",
        {0, 0}) != 0);

    std::optional<CommandInvocation> invocation;
    PluginSpec first;
    first.id = "plugin.ex-first";
    first.displayName = "First Ex owner";
    first.version = "1.0.0";
    first.lazy = false;
    first.commands = {
        {"plugin.ex-first.run", "Run first"}};
    first.activate =
        [&core, &invocation](
            ActivationContext &context,
            std::string &activationError) {
            if (!context.registerCommand(
                    "plugin.ex-first.run",
                    [&invocation](
                        const CommandInvocation &value) {
                        invocation = value;
                        return CommandExecutionResult::success();
                    },
                    &activationError)) {
                return false;
            }
            UserCommandDefinition command{
                "RunFirst",
                "plugin.ex-first.run",
                "Run first"};
            command.nargs = UserCommandNargs::One;
            command.acceptsBang = true;
            return context.registerUserCommand(
                core, std::move(command), &activationError);
        };

    VkCorePluginManager manager;
    std::string error;
    QVERIFY2(manager.addPlugin(std::move(first), &error), error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());
    QVERIFY2(manager.activateStartupPlugins(&error), error.c_str());

    DispatchResult requested = core.submitCommandLine(
        window, CommandLineKind::Ex, u":RunFirst! value");
    const auto command = std::ranges::find_if(
        requested.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        });
    QVERIFY(command != requested.events.cend());
    QVERIFY(manager.executeCommand(CommandInvocation{
        command->commandId,
        command->commandArguments,
        command->count,
        command->countWasExplicit,
        command->mode,
        command->view,
        command->buffer,
        command->inputTarget,
        command->bang,
        command->lineRange,
        command->rawArguments}));
    QVERIFY(invocation.has_value());
    QVERIFY(invocation->bang);
    QCOMPARE(invocation->arguments, std::vector<std::string>({"value"}));

    QVERIFY2(
        manager.disablePlugin("plugin.ex-first", &error),
        error.c_str());
    requested = core.submitCommandLine(
        window, CommandLineKind::Ex, u":RunFirst value");
    QVERIFY(std::ranges::none_of(
        requested.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        }));

    QVERIFY2(
        manager.enablePlugin("plugin.ex-first", &error),
        error.c_str());
    requested = core.submitCommandLine(
        window, CommandLineKind::Ex, u":RunFirst value");
    QVERIFY(std::ranges::any_of(
        requested.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        }));

    PluginSpec conflicting;
    conflicting.id = "plugin.ex-conflict";
    conflicting.displayName = "Conflicting Ex owner";
    conflicting.version = "1.0.0";
    conflicting.lazy = false;
    conflicting.commands = {
        {"plugin.ex-conflict.run", "Run conflict"}};
    conflicting.activate =
        [&core](
            ActivationContext &context,
            std::string &activationError) {
            if (!context.registerCommand(
                    "plugin.ex-conflict.run",
                    [](const CommandInvocation &) {
                        return CommandExecutionResult::success();
                    },
                    &activationError)) {
                return false;
            }
            return context.registerUserCommand(
                core,
                UserCommandDefinition{
                    "RunFirst",
                    "plugin.ex-conflict.run",
                    "Must collide"},
                &activationError);
        };
    VkCorePluginManager conflictManager;
    QVERIFY2(
        conflictManager.addPlugin(
            std::move(conflicting), &error),
        error.c_str());
    QVERIFY2(conflictManager.resolve(&error), error.c_str());
    QVERIFY(!conflictManager.activateStartupPlugins(&error));
    QVERIFY(error.find("already owned") != std::string::npos);
    QVERIFY(!conflictManager.commandRegistry().contains(
        "plugin.ex-conflict.run"));

    // The failed owner never revokes the original generation.
    requested = core.submitCommandLine(
        window, CommandLineKind::Ex, u":RunFirst value");
    QVERIFY(std::ranges::any_of(
        requested.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested
                && event.commandId == "plugin.ex-first.run";
        }));
}

void VkCorePluginManagerTests::
    concurrentActivationAndDisableArePublicationAtomic()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Navigation);
    VkCorePluginManager manager;
    std::mutex gateMutex;
    std::condition_variable gateChanged;
    bool callbackEntered = false;
    bool releaseCallback = false;
    std::atomic_int executions = 0;

    PluginSpec plugin;
    plugin.id = "plugin.concurrent-publication";
    plugin.displayName = "Concurrent publication";
    plugin.version = "1.0.0";
    plugin.lazy = false;
    plugin.commands = {
        {"plugin.concurrent-publication.run", "Run"}};
    plugin.activate =
        [&core,
         window,
         &gateMutex,
         &gateChanged,
         &callbackEntered,
         &releaseCallback,
         &executions](
            ActivationContext &context,
            std::string &error) {
            if (!context.registerCommand(
                    "plugin.concurrent-publication.run",
                    [&executions](const CommandInvocation &) {
                        executions.fetch_add(
                            1, std::memory_order_relaxed);
                        return CommandExecutionResult::success();
                    },
                    &error)) {
                return false;
            }
            MappingDefinition mapping;
            mapping.modes = mappingModes(MappingMode::Normal);
            mapping.lhs = U"z";
            mapping.target = CommandTarget{
                "plugin.concurrent-publication.run"};
            mapping.window = window;
            mapping.metadata.description = "Run";
            if (!context.registerMapping(
                    core, std::move(mapping), &error)) {
                return false;
            }
            if (!context.registerUserCommand(
                    core,
                    UserCommandDefinition{
                        "ConcurrentRun",
                        "plugin.concurrent-publication.run",
                        "Run"},
                    &error)) {
                return false;
            }
            std::unique_lock gateLock(gateMutex);
            callbackEntered = true;
            gateChanged.notify_all();
            gateChanged.wait(
                gateLock,
                [&releaseCallback] {
                    return releaseCallback;
                });
            return true;
        };

    std::string error;
    QVERIFY2(manager.addPlugin(std::move(plugin), &error), error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());

    std::atomic_bool activationSucceeded = false;
    std::atomic_bool disableSucceeded = false;
    std::atomic_bool disableFinished = false;
    std::string activationError;
    std::string disableError;
    std::jthread activationThread(
        [&] {
            activationSucceeded.store(
                manager.activateStartupPlugins(
                    &activationError),
                std::memory_order_release);
        });
    {
        std::unique_lock gateLock(gateMutex);
        QVERIFY(gateChanged.wait_for(
            gateLock,
            std::chrono::seconds(1),
            [&callbackEntered] {
                return callbackEntered;
            }));
    }
    QCOMPARE(
        manager.pluginInfo("plugin.concurrent-publication")
            ->state,
        PluginState::Activating);
    QVERIFY(!manager.commandRegistry().contains(
        "plugin.concurrent-publication.run"));
    QVERIFY(!core.userCommands().contains("ConcurrentRun"));

    std::jthread disableThread(
        [&] {
            disableSucceeded.store(
                manager.disablePlugin(
                    "plugin.concurrent-publication",
                    &disableError),
                std::memory_order_release);
            disableFinished.store(
                true, std::memory_order_release);
        });
    QTest::qWait(20);
    QVERIFY(!disableFinished.load(std::memory_order_acquire));

    std::atomic_bool stopObserver = false;
    std::atomic_bool executedOutsideReady = false;
    std::jthread observer(
        [&] {
            while (!stopObserver.load(
                std::memory_order_acquire)) {
                const auto before = manager.pluginInfo(
                    "plugin.concurrent-publication");
                const auto result =
                    manager.commandRegistry().execute(
                        "plugin.concurrent-publication.run");
                const auto after = manager.pluginInfo(
                    "plugin.concurrent-publication");
                if (result
                    && before && after
                    && before->state != PluginState::Ready
                    && after->state != PluginState::Ready) {
                    executedOutsideReady.store(
                        true, std::memory_order_release);
                }
                std::this_thread::yield();
            }
        });
    {
        const std::scoped_lock gateLock(gateMutex);
        releaseCallback = true;
    }
    gateChanged.notify_all();
    activationThread.join();
    disableThread.join();
    stopObserver.store(true, std::memory_order_release);
    observer.join();

    QVERIFY2(
        activationSucceeded.load(std::memory_order_acquire),
        activationError.c_str());
    QVERIFY2(
        disableSucceeded.load(std::memory_order_acquire),
        disableError.c_str());
    QVERIFY(!executedOutsideReady.load(
        std::memory_order_acquire));
    QCOMPARE(
        manager.pluginInfo("plugin.concurrent-publication")
            ->state,
        PluginState::Disabled);
    QVERIFY(!manager.commandRegistry().contains(
        "plugin.concurrent-publication.run"));
    QVERIFY(!core.userCommands().contains("ConcurrentRun"));
    const QKeyEvent key(
        QEvent::KeyPress,
        Qt::Key_Z,
        Qt::NoModifier,
        QStringLiteral("z"));
    const DispatchResult dispatched =
        core.dispatch(window, 0, key);
    QVERIFY(std::ranges::none_of(
        dispatched.events,
        [](const Event &event) {
            return event.type
                == EventType::CommandRequested;
        }));
}

void VkCorePluginManagerTests::
    cleanupCanReenterAndOldGenerationCannotRevokeNew()
{
    VkCorePluginManager manager;
    std::atomic_int targetExecutions = 0;
    PluginSpec target = commandPlugin(
        "plugin.cleanup-target",
        "plugin.cleanup-target.run",
        nullptr,
        &targetExecutions);

    std::atomic_int cleanupReentries = 0;
    PluginSpec owner = commandPlugin(
        "plugin.generation-owner",
        "plugin.generation-owner.run");
    const auto registerOwnerCommand =
        std::move(owner.activate);
    owner.activate =
        [&manager,
         &cleanupReentries,
         registerOwnerCommand](
            ActivationContext &context,
            std::string &activationError) {
            if (!context.addCleanup(
                    [&manager, &cleanupReentries] {
                        const CommandExecutionResult result =
                            manager.executeCommand(
                                "plugin.cleanup-target.run");
                        if (result) {
                            cleanupReentries.fetch_add(
                                1,
                                std::memory_order_relaxed);
                        }
                    },
                    &activationError)) {
                return false;
            }
            return registerOwnerCommand(
                context, activationError);
        };

    std::string error;
    QVERIFY2(manager.addPlugin(std::move(target), &error), error.c_str());
    QVERIFY2(manager.addPlugin(std::move(owner), &error), error.c_str());
    QVERIFY2(manager.resolve(&error), error.c_str());
    QVERIFY(manager.executeCommand(
        "plugin.generation-owner.run"));
    const auto firstOwner = manager.commandRegistry().ownerOf(
        "plugin.generation-owner.run");
    QVERIFY(firstOwner.has_value());

    QVERIFY2(
        manager.disablePlugin(
            "plugin.generation-owner", &error),
        error.c_str());
    QCOMPARE(cleanupReentries.load(), 1);
    QCOMPARE(targetExecutions.load(), 1);
    QVERIFY(manager.commandRegistry().contains(
        "plugin.cleanup-target.run"));

    QVERIFY2(
        manager.enablePlugin(
            "plugin.generation-owner", &error),
        error.c_str());
    QVERIFY(manager.executeCommand(
        "plugin.generation-owner.run"));
    const auto secondOwner = manager.commandRegistry().ownerOf(
        "plugin.generation-owner.run");
    QVERIFY(secondOwner.has_value());
    QVERIFY(*firstOwner != *secondOwner);

    // Model a late revoker from generation one. Exact generation ownership
    // makes it inert and preserves the current command.
    auto &registry = const_cast<VkCommandRegistry &>(
        manager.commandRegistry());
    QCOMPARE(
        registry.revokeOwner(*firstOwner),
        std::size_t{0});
    QVERIFY(registry.contains(
        "plugin.generation-owner.run"));
    QCOMPARE(
        registry.ownerOf("plugin.generation-owner.run")
            .value(),
        *secondOwner);
}

QTEST_GUILESS_MAIN(VkCorePluginManagerTests)

#include "tst_interaction_plugins.moc"
