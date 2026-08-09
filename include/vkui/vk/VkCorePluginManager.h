#pragma once

#include "VkCommandRegistry.h"
#include "VkTypes.h"
#include "VkUserCommandRegistry.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace vkui::vk {

class VkCore;

using PluginId = std::string;

enum class PluginState
{
    Discovered,
    Resolved,
    Activating,
    Ready,
    Failed,
    Disabled
};

struct CommandDeclaration final
{
    CommandId id;
    std::string description;
};

/**
 * Declarative lazy-activation triggers.
 *
 * These are semantic values emitted after input, buffer and window
 * resolution. They never contain a Qt key event and the plugin manager never
 * synthesizes or replays host input.
 */
struct MappingPrefixTrigger final
{
    /**
     * Exact resolved prefix, or std::nullopt to match every pending prefix.
     *
     * The wildcard is explicit rather than encoded as an empty key string,
     * so a configurable leader (for example <Space>) needs no hard-coded
     * plugin declaration.
     */
    std::optional<std::u32string> exactPrefix;
    MappingModes modes = mappingModes(MappingMode::Normal);
};

struct CoreEventTrigger final
{
    EventType type = EventType::HostAction;
};

struct BufferKindTrigger final
{
    BufferKind kind = BufferKind::Other;
};

struct WindowKindTrigger final
{
    WindowKind kind = WindowKind::Other;
};

using PluginActivationTrigger = std::variant<
    MappingPrefixTrigger,
    CoreEventTrigger,
    BufferKindTrigger,
    WindowKindTrigger>;

/**
 * Typed notifications supplied by VkCore or the host after state resolution.
 *
 * Context ids are carried for observability and future trigger predicates;
 * matching intentionally depends only on the declared semantic kind/prefix.
 */
struct MappingPrefixNotification final
{
    MappingMode mode = MappingMode::Normal;
    std::u32string prefixNotation;
    std::optional<BufferId> buffer;
};

struct CoreEventNotification final
{
    EventType type = EventType::HostAction;
};

struct BufferKindNotification final
{
    BufferId buffer = 0;
    BufferKind kind = BufferKind::Other;
};

struct WindowKindNotification final
{
    WindowId window = 0;
    WindowKind kind = WindowKind::Other;
};

using PluginActivationNotification = std::variant<
    MappingPrefixNotification,
    CoreEventNotification,
    BufferKindNotification,
    WindowKindNotification>;

struct TriggerActivationFailure final
{
    PluginId plugin;
    std::string error;
};

/**
 * Complete result of one trigger notification.
 *
 * One failed plugin does not prevent independent matches from activating.
 * matchedPlugins and readyPlugins follow the resolved topological order.
 */
struct TriggerActivationReport final
{
    std::vector<PluginId> matchedPlugins;
    std::vector<PluginId> readyPlugins;
    std::vector<TriggerActivationFailure> failures;
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
};

class ActivationContext;

using PluginActivationCallback = std::function<
    bool(ActivationContext &, std::string &error)>;

/**
 * Declarative definition for one trusted, in-process core plugin.
 *
 * Dependencies and commands use stable ids rather than UI positions or
 * translated labels. The manager validates the complete graph before any
 * plugin callback runs.
 */
struct PluginSpec final
{
    PluginId id;
    std::string displayName;
    std::string version;
    std::vector<PluginId> dependencies;
    int priority = 0;
    bool lazy = true;
    bool builtin = false;
    bool canDisable = true;
    bool defaultEnabled = true;
    /**
     * Whether the host should expose this spec as an independently managed
     * package. Internal child specs still participate in the full dependency
     * and activation graph, but inherit their user-facing lifecycle from the
     * parent feature.
     */
    bool userVisible = true;
    std::vector<CommandDeclaration> commands;
    std::vector<PluginActivationTrigger> activationTriggers;
    PluginActivationCallback activate;
};

struct PluginInfo final
{
    PluginId id;
    std::string displayName;
    std::string version;
    PluginState state = PluginState::Discovered;
    bool enabled = false;
    bool lazy = true;
    bool builtin = false;
    bool canDisable = true;
    bool userVisible = true;
    std::size_t activationAttempts = 0;
    std::size_t successfulActivations = 0;
    std::chrono::nanoseconds activationDuration{};
    std::string activationReason;
    std::string lastError;
};

/**
 * One exact batch of mappings installed after plugin activation.
 *
 * Floating and dynamically split surfaces do not have a WindowId while their
 * plugin is initially activated. This move-only lease lets those surfaces
 * publish window-local mappings later without bypassing plugin ownership.
 * Resetting the lease revokes only its batch; disabling the owning plugin
 * invalidates every outstanding lease through its RuntimeMappingPool.
 */
class RuntimeMappingLease final
{
public:
    RuntimeMappingLease() = default;
    ~RuntimeMappingLease();

    RuntimeMappingLease(RuntimeMappingLease &&other) noexcept;
    RuntimeMappingLease &operator=(RuntimeMappingLease &&other) noexcept;
    RuntimeMappingLease(const RuntimeMappingLease &) = delete;
    RuntimeMappingLease &operator=(const RuntimeMappingLease &) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] MappingRevocation reset();

private:
    friend class RuntimeMappingPool;
    explicit RuntimeMappingLease(
        std::function<MappingRevocation()> release);

    std::function<MappingRevocation()> m_release;
};

/**
 * Activation-generation-scoped publisher for dynamic window mappings.
 *
 * Pools are created only by ActivationContext. They validate source ownership
 * and command declarations exactly like registerMapping(), batch publication
 * through VkCore, and become permanently inert during rollback/disable.
 */
class RuntimeMappingPool final :
    public std::enable_shared_from_this<RuntimeMappingPool>
{
public:
    RuntimeMappingPool(const RuntimeMappingPool &) = delete;
    RuntimeMappingPool &operator=(const RuntimeMappingPool &) = delete;

    [[nodiscard]] RuntimeMappingLease install(
        std::vector<MappingDefinition> definitions,
        std::string *error = nullptr);
    [[nodiscard]] bool active() const noexcept;

private:
    friend class ActivationContext;

    RuntimeMappingPool(
        PluginId plugin,
        VkCore &core,
        std::vector<CommandId> declaredCommands);
    void deactivate() noexcept;
    [[nodiscard]] MappingRevocation release(std::uint64_t token);

    PluginId m_plugin;
    VkCore *m_core = nullptr;
    std::unordered_set<CommandId> m_declaredCommands;
    mutable std::mutex m_mutex;
    bool m_active = true;
    std::uint64_t m_nextToken = 1;
    std::unordered_map<std::uint64_t, std::vector<MappingId>> m_batches;
};

/**
 * Staging area passed to a plugin activation callback.
 *
 * Registrations are not visible while the callback is running. On success,
 * the manager publishes the complete batch atomically and retains its
 * move-only leases. On failure, nothing reaches the live command registry.
 */
class ActivationContext final
{
public:
    using Cleanup = std::function<void()>;

    ~ActivationContext();

    ActivationContext(const ActivationContext &) = delete;
    ActivationContext &operator=(const ActivationContext &) = delete;
    ActivationContext(ActivationContext &&) = delete;
    ActivationContext &operator=(ActivationContext &&) = delete;

    [[nodiscard]] const PluginId &pluginId() const noexcept;

    [[nodiscard]] bool registerCommand(
        std::string_view id,
        VkCommandRegistry::Handler handler,
        std::string *error = nullptr);

    /**
     * Stages one owner-scoped mapping in the same activation transaction as
     * the plugin's commands.
     *
     * The mapping is not visible until every declared command has validated
     * and the command batch has committed.  Disable, failed activation and
     * shutdown remove only mappings created by this exact plugin activation.
     * This is the trusted in-process equivalent of vim.keymap.set(); external
     * plugins reach it only through validated declarative contributions.
     */
    [[nodiscard]] bool registerMapping(
        VkCore &core,
        MappingDefinition definition,
        std::string *error = nullptr);

    [[nodiscard]] bool registerMappings(
        VkCore &core,
        std::vector<MappingDefinition> definitions,
        std::string *error = nullptr);

    /** Creates a lifecycle-owned publisher for mappings of future windows. */
    [[nodiscard]] std::shared_ptr<RuntimeMappingPool>
    createRuntimeMappingPool(
        VkCore &core,
        std::string *error = nullptr);

    /**
     * Stages a Neovim-style Ex user command for this plugin activation.
     *
     * The name is only an alias: it must target one command declared by this
     * plugin.  Publication is failure-atomic with commands and mappings, and
     * the exact activation lease is revoked on disable or shutdown.
     */
    [[nodiscard]] bool registerUserCommand(
        VkCore &core,
        UserCommandDefinition definition,
        std::string *error = nullptr);

    [[nodiscard]] bool registerUserCommands(
        VkCore &core,
        std::vector<UserCommandDefinition> definitions,
        std::string *error = nullptr);

    /**
     * Stages cleanup for any non-command activation side effect.
     *
     * Cleanups run in reverse registration order. They are rolled back before
     * a failed activation becomes observable, retained after success, and
     * executed outside the manager mutex when the plugin is disabled.
     */
    [[nodiscard]] bool addCleanup(
        Cleanup cleanup,
        std::string *error = nullptr);

private:
    friend class VkCorePluginManager;

    ActivationContext(
        PluginId plugin,
        const std::vector<CommandDeclaration> &declarations);

    [[nodiscard]] bool hasRegistered(
        std::string_view id) const;
    [[nodiscard]] const std::string &registrationError() const noexcept;
    [[nodiscard]] std::vector<
        VkCommandRegistry::CommandRegistration>
    takeRegistrations();
    [[nodiscard]] std::vector<Cleanup> takeCleanups();
    struct StagedMapping final
    {
        VkCore *core = nullptr;
        MappingDefinition definition;
    };
    [[nodiscard]] std::vector<StagedMapping> takeMappings();
    struct StagedUserCommand final
    {
        VkCore *core = nullptr;
        UserCommandDefinition definition;
    };
    [[nodiscard]] std::vector<StagedUserCommand>
    takeUserCommands();
    void rollback() noexcept;

    PluginId m_plugin;
    std::unordered_map<CommandId, std::string> m_declarations;
    std::unordered_set<CommandId> m_registered;
    std::vector<VkCommandRegistry::CommandRegistration>
        m_registrations;
    std::vector<Cleanup> m_cleanups;
    std::vector<StagedMapping> m_mappings;
    std::vector<StagedUserCommand> m_userCommands;
    std::string m_registrationError;
};

/**
 * Dependency-aware lifecycle manager for trusted core plugins.
 *
 * Public operations are thread-safe. Lifecycle callbacks and command
 * handlers always run without the manager mutex held. Concurrent requests
 * for the same lazy plugin share one activation attempt and wait for its
 * terminal state.
 */
class VkCorePluginManager final
{
public:
    VkCorePluginManager();
    ~VkCorePluginManager();

    VkCorePluginManager(const VkCorePluginManager &) = delete;
    VkCorePluginManager &operator=(
        const VkCorePluginManager &) = delete;
    VkCorePluginManager(VkCorePluginManager &&) = delete;
    VkCorePluginManager &operator=(
        VkCorePluginManager &&) = delete;

    [[nodiscard]] bool addPlugin(
        PluginSpec spec,
        std::string *error = nullptr);

    /**
     * Validates and deterministically sorts the complete plugin graph.
     *
     * Dependencies always precede dependents. Among otherwise-ready nodes,
     * higher priority sorts first and plugin id provides the stable tie-break.
     */
    [[nodiscard]] bool resolve(std::string *error = nullptr);
    [[nodiscard]] bool isResolved() const noexcept;
    [[nodiscard]] std::vector<PluginId> topologicalOrder() const;
    /** Returns whether the resolved catalog reserves a semantic command id. */
    [[nodiscard]] bool declaresCommand(
        std::string_view id) const;

    /**
     * Activates every enabled non-lazy plugin in resolved order.
     *
     * A lazy dependency is still activated when an eager plugin requires it.
     */
    [[nodiscard]] bool activateStartupPlugins(
        std::string *error = nullptr);

    /**
     * Resolves the declared owner, activates its dependency closure once,
     * then dispatches through the command registry.
     */
    [[nodiscard]] CommandExecutionResult executeCommand(
        CommandInvocation invocation);
    [[nodiscard]] CommandExecutionResult executeCommand(
        std::string_view id,
        std::vector<std::string> arguments = {});

    /**
     * Activates all enabled plugins matching a resolved semantic trigger.
     *
     * Candidates are snapshotted in resolved order. Each candidate then uses
     * the same exactly-once dependency-aware state machine as lazy commands.
     * Failures are isolated and returned together after every independent
     * candidate has had an opportunity to activate.
     */
    [[nodiscard]] TriggerActivationReport activateTriggered(
        const PluginActivationNotification &notification);

    /**
     * Event-source spelling for activateTriggered().
     *
     * This alias keeps callers declarative: resolve input/state first, notify
     * the manager once, and continue the original logical transaction
     * without reconstructing a Qt key event.
     */
    [[nodiscard]] TriggerActivationReport notify(
        const PluginActivationNotification &notification);

    [[nodiscard]] bool disablePlugin(
        std::string_view id,
        std::string *error = nullptr);
    [[nodiscard]] bool enablePlugin(
        std::string_view id,
        std::string *error = nullptr);

    [[nodiscard]] std::optional<PluginInfo> pluginInfo(
        std::string_view id) const;
    [[nodiscard]] std::vector<PluginInfo> plugins() const;

    [[nodiscard]] const VkCommandRegistry &
    commandRegistry() const noexcept;

private:
    struct Runtime;
    struct MappingTriggerOwner final
    {
        PluginId plugin;
        MappingModes modes = 0;
    };

    [[nodiscard]] bool ensureActivated(
        const PluginId &id,
        std::string reason,
        std::string *error);
    void markActivationFailed(
        const PluginId &id,
        std::uint64_t generation,
        std::chrono::nanoseconds duration,
        std::string message);
    [[nodiscard]] PluginInfo makeInfo(
        const Runtime &runtime) const;

    mutable std::mutex m_mutex;
    // Serializes cross-registry publication/revocation without ever covering
    // plugin callbacks or plugin-owned cleanup code.
    mutable std::mutex m_publicationMutex;
    std::condition_variable m_stateChanged;
    VkCommandRegistry m_commands;
    std::map<PluginId, std::unique_ptr<Runtime>> m_plugins;
    std::vector<PluginId> m_topologicalOrder;
    std::unordered_map<PluginId, std::size_t>
        m_topologicalIndex;
    std::unordered_map<CommandId, PluginId> m_commandOwners;
    std::map<std::u32string, std::vector<MappingTriggerOwner>>
        m_mappingTriggerOwners;
    std::vector<MappingTriggerOwner>
        m_anyMappingTriggerOwners;
    std::map<EventType, std::vector<PluginId>>
        m_eventTriggerOwners;
    std::map<BufferKind, std::vector<PluginId>>
        m_bufferTriggerOwners;
    std::map<WindowKind, std::vector<PluginId>>
        m_windowTriggerOwners;
    bool m_resolved = false;
};

} // namespace vkui::vk
