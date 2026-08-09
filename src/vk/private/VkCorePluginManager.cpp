#include "VkCorePluginManager.h"

#include "VkCore.h"

#include <vkui/core/VkDiagnostics.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <iterator>
#include <limits>
#include <ranges>
#include <set>
#include <sstream>
#include <thread>
#include <type_traits>
#include <utility>

namespace vkui::vk {

namespace {

void setError(std::string *const target, std::string message)
{
    if (target != nullptr) {
        *target = std::move(message);
    }
}

[[nodiscard]] bool isIdentifierCharacter(
    const char value) noexcept
{
    return (value >= 'a' && value <= 'z')
        || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9')
        || value == '.'
        || value == '_'
        || value == '-'
        || value == ':';
}

[[nodiscard]] bool isValidPluginId(
    const std::string_view id) noexcept
{
    return !id.empty()
        && std::ranges::all_of(id, isIdentifierCharacter);
}

[[nodiscard]] constexpr MappingModes allMappingModes() noexcept
{
    return MappingMode::Normal
        | MappingMode::Insert
        | MappingMode::Visual
        | MappingMode::OperatorPending;
}

[[nodiscard]] constexpr bool isValidMappingModes(
    const MappingModes modes) noexcept
{
    return modes != 0
        && (modes & static_cast<MappingModes>(~allMappingModes()))
            == 0;
}

[[nodiscard]] constexpr bool isValidMappingMode(
    const MappingMode mode) noexcept
{
    switch (mode) {
    case MappingMode::Normal:
    case MappingMode::Insert:
    case MappingMode::Visual:
    case MappingMode::OperatorPending:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool isValidEventType(
    const EventType type) noexcept
{
    switch (type) {
    case EventType::HostAction:
    case EventType::ModeChanged:
    case EventType::BufferActivationRequested:
    case EventType::CursorChanged:
    case EventType::ViewportChanged:
    case EventType::BufferEdited:
    case EventType::InsertText:
    case EventType::InsertCommand:
    case EventType::InputError:
    case EventType::StatusMessage:
    case EventType::CommandRequested:
    case EventType::CommandLineRequested:
    case EventType::PromptSubmitted:
    case EventType::PromptCancelled:
    case EventType::DisplayMotionRequested:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool isValidBufferKind(
    const BufferKind kind) noexcept
{
    switch (kind) {
    case BufferKind::Other:
    case BufferKind::Text:
    case BufferKind::Navigation:
    case BufferKind::Reader:
    case BufferKind::Settings:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool isValidWindowKind(
    const WindowKind kind) noexcept
{
    switch (kind) {
    case WindowKind::Other:
    case WindowKind::Navigation:
    case WindowKind::Editor:
    case WindowKind::Surface:
        return true;
    }
    return false;
}

[[nodiscard]] std::string join(
    const std::vector<std::string> &values,
    const std::string_view separator)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            stream << separator;
        }
        stream << values[index];
    }
    return stream.str();
}

struct CleanupReport final
{
    std::size_t failures = 0;
};

[[nodiscard]] CleanupReport runCleanups(
    std::vector<ActivationContext::Cleanup> &cleanups) noexcept
{
    CleanupReport report;
    for (auto cleanup = cleanups.rbegin();
         cleanup != cleanups.rend();
         ++cleanup) {
        try {
            if (*cleanup) {
                (*cleanup)();
            }
        } catch (...) {
            ++report.failures;
        }
    }
    cleanups.clear();
    return report;
}

[[nodiscard]] std::string publicationOwner(
    const PluginId &plugin,
    const std::uint64_t generation)
{
    return plugin + "@" + std::to_string(generation);
}

} // namespace

bool TriggerActivationReport::succeeded() const noexcept
{
    return error.empty() && failures.empty();
}

TriggerActivationReport::operator bool() const noexcept
{
    return succeeded();
}

RuntimeMappingLease::RuntimeMappingLease(
    std::function<MappingRevocation()> release)
    : m_release(std::move(release))
{
}

RuntimeMappingLease::~RuntimeMappingLease()
{
    static_cast<void>(reset());
}

RuntimeMappingLease::RuntimeMappingLease(
    RuntimeMappingLease &&other) noexcept
    : m_release(std::move(other.m_release))
{
    other.m_release = {};
}

RuntimeMappingLease &RuntimeMappingLease::operator=(
    RuntimeMappingLease &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    static_cast<void>(reset());
    m_release = std::move(other.m_release);
    other.m_release = {};
    return *this;
}

RuntimeMappingLease::operator bool() const noexcept
{
    return static_cast<bool>(m_release);
}

MappingRevocation RuntimeMappingLease::reset()
{
    if (!m_release) {
        return {};
    }
    std::function<MappingRevocation()> release =
        std::move(m_release);
    m_release = {};
    return release();
}

RuntimeMappingPool::RuntimeMappingPool(
    PluginId plugin,
    VkCore &core,
    std::vector<CommandId> declaredCommands)
    : m_plugin(std::move(plugin))
    , m_core(&core)
    , m_declaredCommands(
          std::make_move_iterator(declaredCommands.begin()),
          std::make_move_iterator(declaredCommands.end()))
{
}

RuntimeMappingLease RuntimeMappingPool::install(
    std::vector<MappingDefinition> definitions,
    std::string *const error)
{
    std::scoped_lock lock(m_mutex);
    if (!m_active || m_core == nullptr) {
        setError(
            error,
            "plugin '" + m_plugin
                + "' dynamic mapping pool is inactive");
        return {};
    }
    if (definitions.empty()) {
        setError(
            error,
            "plugin '" + m_plugin
                + "' attempted to publish an empty dynamic mapping batch");
        return {};
    }
    for (MappingDefinition &definition : definitions) {
        if (definition.metadata.sourcePlugin.empty()) {
            definition.metadata.sourcePlugin = m_plugin;
        } else if (
            definition.metadata.sourcePlugin != m_plugin) {
            setError(
                error,
                "plugin '" + m_plugin
                    + "' attempted to publish a dynamic mapping owned by '"
                    + definition.metadata.sourcePlugin + "'");
            return {};
        }
        if (const auto *const command =
                std::get_if<CommandTarget>(&definition.target);
            command != nullptr
            && !m_declaredCommands.contains(command->id)) {
            setError(
                error,
                "plugin '" + m_plugin
                    + "' attempted to dynamically map undeclared command '"
                    + command->id + "'");
            return {};
        }
    }

    std::string publicationError;
    std::vector<MappingId> ids =
        m_core->addMappings(definitions, &publicationError);
    if (ids.size() != definitions.size()) {
        setError(
            error,
            publicationError.empty()
                ? "plugin '" + m_plugin
                    + "' could not publish its dynamic mappings"
                : std::move(publicationError));
        return {};
    }
    const std::uint64_t token = m_nextToken++;
    m_batches.emplace(token, std::move(ids));
    const std::weak_ptr<RuntimeMappingPool> weak =
        weak_from_this();
    if (error != nullptr) {
        error->clear();
    }
    return RuntimeMappingLease{
        [weak, token] {
            const std::shared_ptr<RuntimeMappingPool> pool =
                weak.lock();
            return pool == nullptr
                ? MappingRevocation{}
                : pool->release(token);
        }};
}

bool RuntimeMappingPool::active() const noexcept
{
    const std::scoped_lock lock(m_mutex);
    return m_active;
}

MappingRevocation RuntimeMappingPool::release(
    const std::uint64_t token)
{
    std::scoped_lock lock(m_mutex);
    const auto found = m_batches.find(token);
    if (found == m_batches.end()) {
        return {};
    }
    std::vector<MappingId> ids = std::move(found->second);
    m_batches.erase(found);
    return m_core == nullptr
        ? MappingRevocation{}
        : m_core->removeMappings(ids);
}

void RuntimeMappingPool::deactivate() noexcept
{
    std::scoped_lock lock(m_mutex);
    if (!m_active) {
        return;
    }
    m_active = false;
    if (m_core != nullptr) {
        for (const auto &[token, ids] : m_batches) {
            static_cast<void>(token);
            static_cast<void>(m_core->removeMappings(ids));
        }
    }
    m_batches.clear();
    m_core = nullptr;
}

struct VkCorePluginManager::Runtime final
{
    explicit Runtime(PluginSpec value)
        : spec(std::move(value))
    {
    }

    PluginSpec spec;
    PluginState state = PluginState::Discovered;
    bool enabled = false;
    std::size_t activationAttempts = 0;
    std::size_t successfulActivations = 0;
    std::chrono::nanoseconds activationDuration{};
    std::string activationReason;
    std::string lastError;
    std::thread::id activationThread;
    std::uint64_t publicationGeneration = 0;
    std::string publicationOwner;
    std::shared_ptr<std::atomic_bool> publicationLive;
    bool deactivating = false;
    std::thread::id deactivationThread;
    std::vector<VkCommandRegistry::ContributionLease>
        contributions;
    std::vector<ActivationContext::Cleanup>
        publicationCleanups;
    std::vector<ActivationContext::Cleanup> cleanups;
};

ActivationContext::ActivationContext(
    PluginId plugin,
    const std::vector<CommandDeclaration> &declarations)
    : m_plugin(std::move(plugin))
{
    m_declarations.reserve(declarations.size());
    m_registered.reserve(declarations.size());
    m_registrations.reserve(declarations.size());
    m_cleanups.reserve(declarations.size());
    m_mappings.reserve(declarations.size());
    m_userCommands.reserve(declarations.size());
    for (const CommandDeclaration &declaration : declarations) {
        m_declarations.emplace(
            declaration.id,
            declaration.description);
    }
}

ActivationContext::~ActivationContext()
{
    rollback();
}

const PluginId &ActivationContext::pluginId() const noexcept
{
    return m_plugin;
}

bool ActivationContext::registerCommand(
    const std::string_view id,
    VkCommandRegistry::Handler handler,
    std::string *const error)
{
    const CommandId commandId(id);
    const auto declaration = m_declarations.find(commandId);
    if (declaration == m_declarations.end()) {
        m_registrationError =
            "plugin '" + m_plugin
            + "' attempted to register undeclared command '"
            + commandId + "'";
        setError(error, m_registrationError);
        return false;
    }
    if (!handler) {
        m_registrationError =
            "plugin '" + m_plugin + "' command '"
            + commandId + "' has no handler";
        setError(error, m_registrationError);
        return false;
    }
    if (!m_registered.insert(commandId).second) {
        m_registrationError =
            "plugin '" + m_plugin + "' registered command '"
            + commandId + "' more than once";
        setError(error, m_registrationError);
        return false;
    }

    m_registrations.push_back(
        VkCommandRegistry::CommandRegistration{
            CommandDescriptor{
                commandId,
                declaration->second},
            std::move(handler)});
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool ActivationContext::addCleanup(
    Cleanup cleanup,
    std::string *const error)
{
    if (!cleanup) {
        m_registrationError =
            "plugin '" + m_plugin
            + "' attempted to register an empty cleanup";
        setError(error, m_registrationError);
        return false;
    }
    m_cleanups.push_back(std::move(cleanup));
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool ActivationContext::registerMapping(
    VkCore &core,
    MappingDefinition definition,
    std::string *const error)
{
    if (definition.metadata.sourcePlugin.empty()) {
        definition.metadata.sourcePlugin = m_plugin;
    } else if (definition.metadata.sourcePlugin != m_plugin) {
        m_registrationError =
            "plugin '" + m_plugin
            + "' attempted to register a mapping owned by '"
            + definition.metadata.sourcePlugin + "'";
        setError(error, m_registrationError);
        return false;
    }

    if (const auto *const command =
            std::get_if<CommandTarget>(&definition.target);
        command != nullptr
        && !m_declarations.contains(command->id)) {
        m_registrationError =
            "plugin '" + m_plugin
            + "' attempted to map undeclared command '"
            + command->id + "'";
        setError(error, m_registrationError);
        return false;
    }

    m_mappings.push_back(
        StagedMapping{&core, std::move(definition)});
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool ActivationContext::registerMappings(
    VkCore &core,
    std::vector<MappingDefinition> definitions,
    std::string *const error)
{
    const std::size_t originalSize = m_mappings.size();
    for (MappingDefinition &definition : definitions) {
        if (!registerMapping(
                core, std::move(definition), error)) {
            m_mappings.resize(originalSize);
            return false;
        }
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::shared_ptr<RuntimeMappingPool>
ActivationContext::createRuntimeMappingPool(
    VkCore &core,
    std::string *const error)
{
    std::vector<CommandId> declaredCommands;
    declaredCommands.reserve(m_declarations.size());
    for (const auto &[id, description] : m_declarations) {
        static_cast<void>(description);
        declaredCommands.push_back(id);
    }
    auto pool = std::shared_ptr<RuntimeMappingPool>(
        new RuntimeMappingPool(
            m_plugin,
            core,
            std::move(declaredCommands)));
    if (!addCleanup(
            [pool] {
                pool->deactivate();
            },
            error)) {
        pool->deactivate();
        return {};
    }
    if (error != nullptr) {
        error->clear();
    }
    return pool;
}

bool ActivationContext::registerUserCommand(
    VkCore &core,
    UserCommandDefinition definition,
    std::string *const error)
{
    if (!VkUserCommandRegistry::isValidName(definition.name)) {
        m_registrationError =
            "plugin '" + m_plugin
            + "' attempted to register invalid user command '"
            + definition.name + "'";
        setError(error, m_registrationError);
        return false;
    }
    if (!m_declarations.contains(definition.target)) {
        m_registrationError =
            "plugin '" + m_plugin
            + "' attempted to expose undeclared command '"
            + definition.target + "' as an Ex user command";
        setError(error, m_registrationError);
        return false;
    }
    if (definition.description.empty()) {
        definition.description =
            m_declarations.at(definition.target);
    }
    m_userCommands.push_back(
        StagedUserCommand{&core, std::move(definition)});
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool ActivationContext::registerUserCommands(
    VkCore &core,
    std::vector<UserCommandDefinition> definitions,
    std::string *const error)
{
    const std::size_t originalSize = m_userCommands.size();
    for (UserCommandDefinition &definition : definitions) {
        if (!registerUserCommand(
                core, std::move(definition), error)) {
            m_userCommands.resize(originalSize);
            return false;
        }
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool ActivationContext::hasRegistered(
    const std::string_view id) const
{
    return m_registered.contains(std::string(id));
}

const std::string &
ActivationContext::registrationError() const noexcept
{
    return m_registrationError;
}

std::vector<VkCommandRegistry::CommandRegistration>
ActivationContext::takeRegistrations()
{
    return std::move(m_registrations);
}

std::vector<ActivationContext::Cleanup>
ActivationContext::takeCleanups()
{
    return std::move(m_cleanups);
}

std::vector<ActivationContext::StagedMapping>
ActivationContext::takeMappings()
{
    return std::exchange(m_mappings, {});
}

std::vector<ActivationContext::StagedUserCommand>
ActivationContext::takeUserCommands()
{
    return std::exchange(m_userCommands, {});
}

void ActivationContext::rollback() noexcept
{
    static_cast<void>(runCleanups(m_cleanups));
}

VkCorePluginManager::VkCorePluginManager() = default;

VkCorePluginManager::~VkCorePluginManager()
{
    struct DeactivationBatch final
    {
        std::string owner;
        std::shared_ptr<std::atomic_bool> live;
        std::vector<VkCommandRegistry::ContributionLease>
            contributions;
        std::vector<ActivationContext::Cleanup>
            publicationCleanups;
        std::vector<ActivationContext::Cleanup> cleanups;
    };

    std::vector<DeactivationBatch> batches;
    {
        const std::scoped_lock lock(m_mutex);
        std::vector<PluginId> order = m_topologicalOrder;
        if (order.empty()) {
            order.reserve(m_plugins.size());
            for (const auto &[id, runtime] : m_plugins) {
                static_cast<void>(runtime);
                order.push_back(id);
            }
        }
        batches.reserve(order.size());
        for (auto plugin = order.rbegin();
             plugin != order.rend();
             ++plugin) {
            Runtime &runtime = *m_plugins.at(*plugin);
            runtime.enabled = false;
            runtime.state = PluginState::Disabled;
            if (runtime.publicationLive) {
                runtime.publicationLive->store(
                    false, std::memory_order_release);
            }
            batches.push_back(
                DeactivationBatch{
                    std::move(runtime.publicationOwner),
                    std::move(runtime.publicationLive),
                    std::move(runtime.contributions),
                    std::move(runtime.publicationCleanups),
                    std::move(runtime.cleanups)});
        }
    }

    // Revoke every callable entry and host-owned side effect as serialized
    // owner batches. Plugin cleanup remains outside this lock because it may
    // safely inspect or re-enter the manager.
    {
        const std::scoped_lock publicationLock(
            m_publicationMutex);
        for (DeactivationBatch &batch : batches) {
            if (!batch.owner.empty()) {
                m_commands.revokeOwner(batch.owner);
            }
            batch.contributions.clear();
            static_cast<void>(
                runCleanups(batch.publicationCleanups));
        }
    }
    for (DeactivationBatch &batch : batches) {
        static_cast<void>(runCleanups(batch.cleanups));
    }
}

bool VkCorePluginManager::addPlugin(
    PluginSpec spec,
    std::string *const error)
{
    if (!isValidPluginId(spec.id)) {
        setError(error, "invalid plugin id '" + spec.id + "'");
        return false;
    }
    if (spec.displayName.empty()) {
        setError(
            error,
            "plugin '" + spec.id
                + "' must have a display name");
        return false;
    }
    if (spec.version.empty()) {
        setError(
            error,
            "plugin '" + spec.id + "' must have a version");
        return false;
    }
    if (!spec.canDisable && !spec.defaultEnabled) {
        setError(
            error,
            "required plugin '" + spec.id
                + "' cannot be disabled by default");
        return false;
    }

    std::unordered_set<PluginId> dependencies;
    dependencies.reserve(spec.dependencies.size());
    for (const PluginId &dependency : spec.dependencies) {
        if (!isValidPluginId(dependency)) {
            setError(
                error,
                "plugin '" + spec.id
                    + "' has invalid dependency id '"
                    + dependency + "'");
            return false;
        }
        if (!dependencies.insert(dependency).second) {
            setError(
                error,
                "plugin '" + spec.id
                    + "' declares dependency '" + dependency
                    + "' more than once");
            return false;
        }
    }

    std::unordered_set<CommandId> commands;
    commands.reserve(spec.commands.size());
    for (const CommandDeclaration &command : spec.commands) {
        if (!VkCommandRegistry::isValidCommandId(command.id)) {
            setError(
                error,
                "plugin '" + spec.id
                    + "' declares invalid command id '"
                    + command.id + "'");
            return false;
        }
        if (command.description.empty()) {
            setError(
                error,
                "plugin '" + spec.id + "' command '"
                    + command.id
                    + "' must have a description");
            return false;
        }
        if (!commands.insert(command.id).second) {
            setError(
                error,
                "plugin '" + spec.id + "' declares command '"
                    + command.id + "' more than once");
            return false;
        }
    }

    std::map<std::optional<std::u32string>, MappingModes>
        declaredMappingPrefixes;
    std::set<EventType> declaredEvents;
    std::set<BufferKind> declaredBufferKinds;
    std::set<WindowKind> declaredWindowKinds;
    for (const PluginActivationTrigger &trigger :
         spec.activationTriggers) {
        bool valid = true;
        std::string validationError;
        std::visit(
            [&]<typename Trigger>(const Trigger &value) {
                using Value = std::decay_t<Trigger>;
                if constexpr (
                    std::is_same_v<
                        Value,
                        MappingPrefixTrigger>) {
                    if (value.exactPrefix.has_value()
                        && value.exactPrefix->empty()) {
                        valid = false;
                        validationError =
                            "mapping-prefix exact match must not be empty";
                        return;
                    }
                    if (!isValidMappingModes(value.modes)) {
                        valid = false;
                        validationError =
                            "mapping-prefix trigger has an invalid mode mask";
                        return;
                    }
                    MappingModes &declared =
                        declaredMappingPrefixes[
                            value.exactPrefix];
                    if ((declared & value.modes) != 0) {
                        valid = false;
                        validationError =
                            "mapping-prefix trigger overlaps an earlier declaration";
                        return;
                    }
                    declared = static_cast<MappingModes>(
                        declared | value.modes);
                } else if constexpr (
                    std::is_same_v<Value, CoreEventTrigger>) {
                    if (!isValidEventType(value.type)) {
                        valid = false;
                        validationError =
                            "event trigger has an invalid event type";
                    } else if (!declaredEvents.insert(
                                   value.type)
                                   .second) {
                        valid = false;
                        validationError =
                            "event trigger is declared more than once";
                    }
                } else if constexpr (
                    std::is_same_v<Value, BufferKindTrigger>) {
                    if (!isValidBufferKind(value.kind)) {
                        valid = false;
                        validationError =
                            "buffer trigger has an invalid buffer kind";
                    } else if (!declaredBufferKinds.insert(
                                   value.kind)
                                   .second) {
                        valid = false;
                        validationError =
                            "buffer trigger is declared more than once";
                    }
                } else if constexpr (
                    std::is_same_v<Value, WindowKindTrigger>) {
                    if (!isValidWindowKind(value.kind)) {
                        valid = false;
                        validationError =
                            "window trigger has an invalid window kind";
                    } else if (!declaredWindowKinds.insert(
                                   value.kind)
                                   .second) {
                        valid = false;
                        validationError =
                            "window trigger is declared more than once";
                    }
                }
            },
            trigger);
        if (!valid) {
            setError(
                error,
                "plugin '" + spec.id + "' "
                    + validationError);
            return false;
        }
    }

    const std::scoped_lock lock(m_mutex);
    if (m_resolved) {
        setError(
            error,
            "plugins cannot be added after the graph is resolved");
        return false;
    }
    if (m_plugins.contains(spec.id)) {
        setError(
            error,
            "plugin '" + spec.id + "' is already registered");
        return false;
    }
    const PluginId id = spec.id;
    m_plugins.emplace(
        id,
        std::make_unique<Runtime>(std::move(spec)));
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool VkCorePluginManager::resolve(std::string *const error)
{
    const std::scoped_lock lock(m_mutex);
    if (m_resolved) {
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    std::map<PluginId, std::vector<PluginId>> dependents;
    std::map<PluginId, std::size_t> indegrees;
    for (const auto &[id, runtime] : m_plugins) {
        indegrees.emplace(id, runtime->spec.dependencies.size());
        dependents.try_emplace(id);
        for (const PluginId &dependency :
             runtime->spec.dependencies) {
            if (!m_plugins.contains(dependency)) {
                setError(
                    error,
                    "plugin '" + id
                        + "' is missing dependency '"
                        + dependency + "'");
                return false;
            }
            dependents[dependency].push_back(id);
        }
    }
    for (auto &[id, values] : dependents) {
        static_cast<void>(id);
        std::ranges::sort(values);
    }

    const auto precedes =
        [this](const PluginId &lhs, const PluginId &rhs) {
            const int lhsPriority =
                m_plugins.at(lhs)->spec.priority;
            const int rhsPriority =
                m_plugins.at(rhs)->spec.priority;
            if (lhsPriority != rhsPriority) {
                return lhsPriority > rhsPriority;
            }
            return lhs < rhs;
        };

    std::vector<PluginId> ready;
    ready.reserve(m_plugins.size());
    for (const auto &[id, indegree] : indegrees) {
        if (indegree == 0) {
            ready.push_back(id);
        }
    }
    std::ranges::sort(ready, precedes);

    std::vector<PluginId> order;
    order.reserve(m_plugins.size());
    while (!ready.empty()) {
        PluginId id = std::move(ready.front());
        ready.erase(ready.begin());
        order.push_back(id);

        for (const PluginId &dependent : dependents[id]) {
            std::size_t &indegree = indegrees[dependent];
            --indegree;
            if (indegree == 0) {
                ready.push_back(dependent);
            }
        }
        std::ranges::sort(ready, precedes);
    }

    if (order.size() != m_plugins.size()) {
        std::vector<PluginId> cyclic;
        for (const auto &[id, indegree] : indegrees) {
            if (indegree != 0) {
                cyclic.push_back(id);
            }
        }
        setError(
            error,
            "dependency cycle detected among plugins: "
                + join(cyclic, ", "));
        return false;
    }

    std::unordered_map<CommandId, PluginId> commandOwners;
    for (const auto &[id, runtime] : m_plugins) {
        for (const CommandDeclaration &command :
             runtime->spec.commands) {
            const auto [existing, inserted] =
                commandOwners.emplace(command.id, id);
            if (!inserted) {
                setError(
                    error,
                    "command '" + command.id
                        + "' is declared by both plugin '"
                        + existing->second + "' and plugin '"
                        + id + "'");
                return false;
            }
        }
    }

    std::map<std::u32string, std::vector<MappingTriggerOwner>>
        mappingTriggerOwners;
    std::vector<MappingTriggerOwner>
        anyMappingTriggerOwners;
    std::map<EventType, std::vector<PluginId>>
        eventTriggerOwners;
    std::map<BufferKind, std::vector<PluginId>>
        bufferTriggerOwners;
    std::map<WindowKind, std::vector<PluginId>>
        windowTriggerOwners;
    for (const PluginId &id : order) {
        const Runtime &runtime = *m_plugins.at(id);
        for (const PluginActivationTrigger &trigger :
             runtime.spec.activationTriggers) {
            std::visit(
                [&]<typename Trigger>(const Trigger &value) {
                    using Value = std::decay_t<Trigger>;
                    if constexpr (
                        std::is_same_v<
                            Value,
                            MappingPrefixTrigger>) {
                        MappingTriggerOwner owner{
                            id,
                            value.modes};
                        if (value.exactPrefix.has_value()) {
                            mappingTriggerOwners[
                                *value.exactPrefix]
                                .push_back(std::move(owner));
                        } else {
                            anyMappingTriggerOwners.push_back(
                                std::move(owner));
                        }
                    } else if constexpr (
                        std::is_same_v<
                            Value,
                            CoreEventTrigger>) {
                        eventTriggerOwners[value.type]
                            .push_back(id);
                    } else if constexpr (
                        std::is_same_v<
                            Value,
                            BufferKindTrigger>) {
                        bufferTriggerOwners[value.kind]
                            .push_back(id);
                    } else if constexpr (
                        std::is_same_v<
                            Value,
                            WindowKindTrigger>) {
                        windowTriggerOwners[value.kind]
                            .push_back(id);
                    }
                },
                trigger);
        }
    }

    m_topologicalOrder = std::move(order);
    m_topologicalIndex.clear();
    m_topologicalIndex.reserve(m_topologicalOrder.size());
    for (std::size_t index = 0;
         index < m_topologicalOrder.size();
         ++index) {
        m_topologicalIndex.emplace(
            m_topologicalOrder[index],
            index);
    }
    m_commandOwners = std::move(commandOwners);
    m_mappingTriggerOwners =
        std::move(mappingTriggerOwners);
    m_anyMappingTriggerOwners =
        std::move(anyMappingTriggerOwners);
    m_eventTriggerOwners = std::move(eventTriggerOwners);
    m_bufferTriggerOwners = std::move(bufferTriggerOwners);
    m_windowTriggerOwners = std::move(windowTriggerOwners);
    for (auto &[id, runtime] : m_plugins) {
        static_cast<void>(id);
        runtime->enabled = runtime->spec.defaultEnabled;
        runtime->state = runtime->enabled
            ? PluginState::Resolved
            : PluginState::Disabled;
    }
    m_resolved = true;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool VkCorePluginManager::isResolved() const noexcept
{
    const std::scoped_lock lock(m_mutex);
    return m_resolved;
}

std::vector<PluginId>
VkCorePluginManager::topologicalOrder() const
{
    const std::scoped_lock lock(m_mutex);
    return m_topologicalOrder;
}

bool VkCorePluginManager::declaresCommand(
    const std::string_view id) const
{
    const std::scoped_lock lock(m_mutex);
    return m_commandOwners.contains(std::string(id));
}

bool VkCorePluginManager::activateStartupPlugins(
    std::string *const error)
{
    std::vector<PluginId> order;
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_resolved) {
            setError(
                error,
                "plugin graph has not been resolved");
            return false;
        }
        order = m_topologicalOrder;
    }

    for (const PluginId &id : order) {
        bool shouldActivate = false;
        {
            const std::scoped_lock lock(m_mutex);
            const Runtime &runtime = *m_plugins.at(id);
            shouldActivate =
                runtime.enabled && !runtime.spec.lazy;
        }
        if (shouldActivate
            && !ensureActivated(id, "startup", error)) {
            return false;
        }
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

CommandExecutionResult VkCorePluginManager::executeCommand(
    CommandInvocation invocation)
{
    vkui::DiagnosticSpan commandSpan(
        QStringLiteral("vkcore.command"),
        QStringLiteral("execute"),
        {
            {QStringLiteral("command"),
             QString::fromStdString(invocation.id)},
            {QStringLiteral("window"),
             static_cast<qint64>(invocation.window)},
            {QStringLiteral("buffer"),
             static_cast<qint64>(invocation.buffer)},
            {QStringLiteral("count"),
             static_cast<qint64>(invocation.count)},
        });
    PluginId owner;
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_resolved) {
            commandSpan.fail(QStringLiteral(
                "plugin graph has not been resolved"));
            return CommandExecutionResult::failure(
                "plugin graph has not been resolved");
        }
        const auto command = m_commandOwners.find(invocation.id);
        if (command == m_commandOwners.end()) {
            commandSpan.fail(QStringLiteral(
                "command is not declared"));
            return CommandExecutionResult::failure(
                "command '" + invocation.id
                    + "' is not declared by any plugin");
        }
        owner = command->second;
    }

    std::string error;
    if (!ensureActivated(
            owner,
            "command:" + invocation.id,
            &error)) {
        commandSpan.fail(QString::fromStdString(error));
        return CommandExecutionResult::failure(
            std::move(error));
    }

    CommandExecutionResult result =
        m_commands.execute(invocation);
    if (!result
        && result.error.find("is not registered")
            != std::string::npos) {
        result.error =
            "command '" + invocation.id
            + "' became unavailable while plugin '"
            + owner + "' changed state";
    }
    commandSpan.annotate(
        QStringLiteral("owner"),
        QString::fromStdString(owner));
    if (!result) {
        commandSpan.fail(QString::fromStdString(result.error));
    }
    return result;
}

CommandExecutionResult VkCorePluginManager::executeCommand(
    const std::string_view id,
    std::vector<std::string> arguments)
{
    return executeCommand(
        CommandInvocation{
            std::string(id),
            std::move(arguments)});
}

TriggerActivationReport
VkCorePluginManager::activateTriggered(
    const PluginActivationNotification &notification)
{
    TriggerActivationReport report;
    std::vector<PluginId> candidates;
    std::string reason;
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_resolved) {
            report.error =
                "plugin graph has not been resolved";
            return report;
        }

        const auto appendEnabled =
            [this, &candidates](const PluginId &id) {
                const Runtime &runtime = *m_plugins.at(id);
                if (runtime.enabled) {
                    candidates.push_back(id);
                }
            };

        bool valid = true;
        std::visit(
            [&]<typename Notification>(
                const Notification &value) {
                using Value =
                    std::decay_t<Notification>;
                if constexpr (
                    std::is_same_v<
                        Value,
                        MappingPrefixNotification>) {
                    reason = "trigger:mapping-prefix";
                    if (value.prefixNotation.empty()
                        || !isValidMappingMode(value.mode)) {
                        valid = false;
                        report.error =
                            "invalid mapping-prefix notification";
                        return;
                    }
                    const auto found =
                        m_mappingTriggerOwners.find(
                            value.prefixNotation);
                    const MappingModes currentMode =
                        mappingModes(value.mode);
                    for (const MappingTriggerOwner &owner :
                         m_anyMappingTriggerOwners) {
                        if ((owner.modes & currentMode) != 0) {
                            appendEnabled(owner.plugin);
                        }
                    }
                    if (found
                        != m_mappingTriggerOwners.end()) {
                        for (const MappingTriggerOwner &owner :
                             found->second) {
                            if ((owner.modes & currentMode)
                                != 0) {
                                appendEnabled(owner.plugin);
                            }
                        }
                    }
                } else if constexpr (
                    std::is_same_v<
                        Value,
                        CoreEventNotification>) {
                    reason = "trigger:event";
                    if (!isValidEventType(value.type)) {
                        valid = false;
                        report.error =
                            "invalid core-event notification";
                        return;
                    }
                    const auto found =
                        m_eventTriggerOwners.find(value.type);
                    if (found == m_eventTriggerOwners.end()) {
                        return;
                    }
                    for (const PluginId &id : found->second) {
                        appendEnabled(id);
                    }
                } else if constexpr (
                    std::is_same_v<
                        Value,
                        BufferKindNotification>) {
                    reason = "trigger:buffer-kind";
                    if (!isValidBufferKind(value.kind)) {
                        valid = false;
                        report.error =
                            "invalid buffer-kind notification";
                        return;
                    }
                    const auto found =
                        m_bufferTriggerOwners.find(value.kind);
                    if (found == m_bufferTriggerOwners.end()) {
                        return;
                    }
                    for (const PluginId &id : found->second) {
                        appendEnabled(id);
                    }
                } else if constexpr (
                    std::is_same_v<
                        Value,
                        WindowKindNotification>) {
                    reason = "trigger:window-kind";
                    if (!isValidWindowKind(value.kind)) {
                        valid = false;
                        report.error =
                            "invalid window-kind notification";
                        return;
                    }
                    const auto found =
                        m_windowTriggerOwners.find(value.kind);
                    if (found == m_windowTriggerOwners.end()) {
                        return;
                    }
                    for (const PluginId &id : found->second) {
                        appendEnabled(id);
                    }
                }
            },
            notification);
        if (!valid) {
            return report;
        }

        // Indices are built in topological order. Sorting defensively keeps
        // the public order stable if an index implementation changes later.
        std::ranges::sort(
            candidates,
            [this](const PluginId &lhs, const PluginId &rhs) {
                return m_topologicalIndex.at(lhs)
                    < m_topologicalIndex.at(rhs);
            });
        candidates.erase(
            std::ranges::unique(candidates).begin(),
            candidates.end());
    }

    report.matchedPlugins = candidates;
    report.readyPlugins.reserve(candidates.size());
    report.failures.reserve(candidates.size());
    for (const PluginId &id : candidates) {
        std::string activationError;
        if (ensureActivated(id, reason, &activationError)) {
            report.readyPlugins.push_back(id);
        } else {
            report.failures.push_back(
                TriggerActivationFailure{
                    id,
                    std::move(activationError)});
        }
    }
    return report;
}

TriggerActivationReport VkCorePluginManager::notify(
    const PluginActivationNotification &notification)
{
    return activateTriggered(notification);
}

bool VkCorePluginManager::disablePlugin(
    const std::string_view id,
    std::string *const error)
{
    std::string publicationOwnerId;
    std::shared_ptr<std::atomic_bool> publicationLive;
    std::vector<VkCommandRegistry::ContributionLease>
        contributions;
    std::vector<ActivationContext::Cleanup>
        publicationCleanups;
    std::vector<ActivationContext::Cleanup> cleanups;
    const PluginId plugin(id);
    {
        std::unique_lock lock(m_mutex);
        if (!m_resolved) {
            setError(
                error,
                "plugin graph has not been resolved");
            return false;
        }
        const auto found = m_plugins.find(plugin);
        if (found == m_plugins.end()) {
            setError(
                error,
                "plugin '" + plugin + "' was not found");
            return false;
        }
        Runtime &runtime = *found->second;
        while (runtime.state == PluginState::Activating
               || runtime.deactivating) {
            if ((runtime.state == PluginState::Activating
                 && runtime.activationThread
                    == std::this_thread::get_id())
                || (runtime.deactivating
                    && runtime.deactivationThread
                        == std::this_thread::get_id())) {
                setError(
                    error,
                    "plugin '" + plugin
                        + "' cannot recursively disable itself");
                return false;
            }
            m_stateChanged.wait(
                lock,
                [&runtime] {
                    return runtime.state
                            != PluginState::Activating
                        && !runtime.deactivating;
                });
        }
        if (runtime.state == PluginState::Disabled) {
            if (error != nullptr) {
                error->clear();
            }
            return true;
        }
        if (!runtime.spec.canDisable) {
            setError(
                error,
                "plugin '" + plugin
                    + "' is required and cannot be disabled");
            return false;
        }

        std::vector<PluginId> enabledDependents;
        for (const auto &[candidateId, candidate] :
             m_plugins) {
            if (!candidate->enabled
                || candidateId == plugin) {
                continue;
            }
            if (std::ranges::find(
                    candidate->spec.dependencies,
                    plugin)
                != candidate->spec.dependencies.end()) {
                enabledDependents.push_back(candidateId);
            }
        }
        if (!enabledDependents.empty()) {
            setError(
                error,
                "plugin '" + plugin
                    + "' is required by enabled plugin(s): "
                    + join(enabledDependents, ", "));
            return false;
        }

        runtime.enabled = false;
        runtime.state = PluginState::Disabled;
        runtime.activationThread = {};
        runtime.deactivating = true;
        runtime.deactivationThread =
            std::this_thread::get_id();
        if (runtime.publicationLive) {
            runtime.publicationLive->store(
                false, std::memory_order_release);
        }
        publicationOwnerId =
            std::move(runtime.publicationOwner);
        publicationLive =
            std::move(runtime.publicationLive);
        contributions = std::move(runtime.contributions);
        publicationCleanups =
            std::move(runtime.publicationCleanups);
        cleanups = std::move(runtime.cleanups);
    }

    // Revoke the exact activation generation as one serialized publication
    // transaction. A stale generation can never remove a later re-enable.
    {
        const std::scoped_lock publicationLock(
            m_publicationMutex);
        if (!publicationOwnerId.empty()) {
            m_commands.revokeOwner(publicationOwnerId);
        }
        contributions.clear();
        static_cast<void>(runCleanups(publicationCleanups));
    }
    publicationLive.reset();
    const CleanupReport cleanupReport = runCleanups(cleanups);
    {
        const std::scoped_lock lock(m_mutex);
        Runtime &runtime = *m_plugins.at(plugin);
        runtime.deactivating = false;
        runtime.deactivationThread = {};
        if (cleanupReport.failures != 0) {
            runtime.lastError =
                std::to_string(cleanupReport.failures)
                + " cleanup callback(s) failed while disabling";
        }
    }
    m_stateChanged.notify_all();
    // Revocation and the Disabled state are committed before cleanups run.
    // A cleanup can report a diagnostic, but it cannot safely restore commands
    // or replay already-completed cleanups. Report the committed transition as
    // success so callers persist the same enabled state that the runtime now
    // exposes. pluginInfo().lastError retains the cleanup diagnostic.
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool VkCorePluginManager::enablePlugin(
    const std::string_view id,
    std::string *const error)
{
    PluginId plugin(id);
    bool activateImmediately = false;
    {
        std::unique_lock lock(m_mutex);
        if (!m_resolved) {
            setError(
                error,
                "plugin graph has not been resolved");
            return false;
        }
        const auto found = m_plugins.find(plugin);
        if (found == m_plugins.end()) {
            setError(
                error,
                "plugin '" + plugin + "' was not found");
            return false;
        }
        Runtime &runtime = *found->second;
        while (runtime.deactivating) {
            if (runtime.deactivationThread
                == std::this_thread::get_id()) {
                setError(
                    error,
                    "plugin '" + plugin
                        + "' cannot enable itself from its cleanup");
                return false;
            }
            m_stateChanged.wait(
                lock,
                [&runtime] {
                    return !runtime.deactivating;
                });
        }
        if (runtime.enabled
            && runtime.state != PluginState::Disabled
            && runtime.state != PluginState::Failed) {
            if (error != nullptr) {
                error->clear();
            }
            return true;
        }
        for (const PluginId &dependency :
             runtime.spec.dependencies) {
            if (!m_plugins.at(dependency)->enabled) {
                setError(
                    error,
                    "plugin '" + plugin
                        + "' cannot be enabled because dependency '"
                        + dependency + "' is disabled");
                return false;
            }
        }

        runtime.enabled = true;
        runtime.state = PluginState::Resolved;
        runtime.lastError.clear();
        runtime.activationDuration = {};
        runtime.activationReason.clear();
        activateImmediately = !runtime.spec.lazy;
    }

    if (activateImmediately
        && !ensureActivated(plugin, "enable", error)) {
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::optional<PluginInfo> VkCorePluginManager::pluginInfo(
    const std::string_view id) const
{
    const std::scoped_lock lock(m_mutex);
    const auto found = m_plugins.find(std::string(id));
    if (found == m_plugins.end()) {
        return std::nullopt;
    }
    return makeInfo(*found->second);
}

std::vector<PluginInfo> VkCorePluginManager::plugins() const
{
    std::vector<PluginInfo> result;
    const std::scoped_lock lock(m_mutex);
    result.reserve(m_plugins.size());
    for (const auto &[id, runtime] : m_plugins) {
        static_cast<void>(id);
        result.push_back(makeInfo(*runtime));
    }
    return result;
}

const VkCommandRegistry &
VkCorePluginManager::commandRegistry() const noexcept
{
    return m_commands;
}

bool VkCorePluginManager::ensureActivated(
    const PluginId &id,
    std::string reason,
    std::string *const error)
{
    PluginActivationCallback callback;
    std::vector<PluginId> dependencies;
    std::vector<CommandDeclaration> declarations;
    std::uint64_t activationGeneration = 0;
    std::string activationOwnerId;
    std::shared_ptr<std::atomic_bool> activationLive;

    {
        std::unique_lock lock(m_mutex);
        if (!m_resolved) {
            setError(
                error,
                "plugin graph has not been resolved");
            return false;
        }
        const auto found = m_plugins.find(id);
        if (found == m_plugins.end()) {
            setError(
                error,
                "plugin '" + id + "' was not found");
            return false;
        }
        Runtime &runtime = *found->second;
        for (;;) {
            switch (runtime.state) {
            case PluginState::Ready:
                if (error != nullptr) {
                    error->clear();
                }
                return true;
            case PluginState::Disabled:
                setError(
                    error,
                    "plugin '" + id + "' is disabled");
                return false;
            case PluginState::Failed:
                setError(
                    error,
                    "plugin '" + id + "' failed to activate: "
                        + runtime.lastError);
                return false;
            case PluginState::Discovered:
                setError(
                    error,
                    "plugin '" + id + "' is not resolved");
                return false;
            case PluginState::Activating:
                if (runtime.activationThread
                    == std::this_thread::get_id()) {
                    setError(
                        error,
                        "recursive activation detected for plugin '"
                            + id + "'");
                    return false;
                }
                m_stateChanged.wait(
                    lock,
                    [&runtime] {
                        return runtime.state
                            != PluginState::Activating;
                    });
                continue;
            case PluginState::Resolved:
                if (runtime.publicationGeneration
                    == std::numeric_limits<
                        std::uint64_t>::max()) {
                    runtime.state = PluginState::Failed;
                    runtime.lastError =
                        "plugin activation generation space is exhausted";
                    setError(error, runtime.lastError);
                    return false;
                }
                ++runtime.publicationGeneration;
                activationGeneration =
                    runtime.publicationGeneration;
                activationOwnerId = publicationOwner(
                    id, activationGeneration);
                activationLive =
                    std::make_shared<std::atomic_bool>(false);
                runtime.publicationOwner = activationOwnerId;
                runtime.publicationLive = activationLive;
                runtime.state = PluginState::Activating;
                runtime.activationThread =
                    std::this_thread::get_id();
                runtime.activationReason = std::move(reason);
                runtime.lastError.clear();
                ++runtime.activationAttempts;
                callback = runtime.spec.activate;
                dependencies = runtime.spec.dependencies;
                declarations = runtime.spec.commands;
                break;
            }
            break;
        }

        std::ranges::sort(
            dependencies,
            [this](const PluginId &lhs, const PluginId &rhs) {
                return m_topologicalIndex.at(lhs)
                    < m_topologicalIndex.at(rhs);
            });
    }

    vkui::DiagnosticSpan activationSpan(
        QStringLiteral("vkcore.plugin"),
        QStringLiteral("activate"),
        {
            {QStringLiteral("plugin"), QString::fromStdString(id)},
            {QStringLiteral("generation"),
             static_cast<qint64>(activationGeneration)},
            {QStringLiteral("dependencies"),
             static_cast<qint64>(dependencies.size())},
        });

    for (const PluginId &dependency : dependencies) {
        std::string dependencyError;
        if (!ensureActivated(
                dependency,
                "dependency:" + id,
                &dependencyError)) {
            const std::string message =
                "dependency '" + dependency
                + "' could not activate: " + dependencyError;
            markActivationFailed(
                id, activationGeneration, {}, message);
            activationSpan.fail(QString::fromStdString(message));
            setError(error, message);
            return false;
        }
    }

    ActivationContext context(id, declarations);
    bool callbackSucceeded = true;
    std::string callbackError;
    const auto started = std::chrono::steady_clock::now();
    try {
        if (callback) {
            callbackSucceeded =
                callback(context, callbackError);
        }
    } catch (const std::exception &exception) {
        callbackSucceeded = false;
        callbackError =
            "activation callback threw an exception: "
            + std::string(exception.what());
    } catch (...) {
        callbackSucceeded = false;
        callbackError =
            "activation callback threw an unknown exception";
    }
    const auto elapsed = [started] {
        return std::chrono::duration_cast<
            std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started);
    };

    if (callbackSucceeded
        && !context.registrationError().empty()) {
        callbackSucceeded = false;
        callbackError = context.registrationError();
    }
    if (callbackSucceeded) {
        for (const CommandDeclaration &declaration :
             declarations) {
            if (!context.hasRegistered(declaration.id)) {
                callbackSucceeded = false;
                callbackError =
                    "plugin '" + id
                    + "' did not register declared command '"
                    + declaration.id + "'";
                break;
            }
        }
    }
    if (!callbackSucceeded) {
        if (callbackError.empty()) {
            callbackError =
                "activation callback returned failure";
        }
        context.rollback();
        markActivationFailed(
            id,
            activationGeneration,
            elapsed(),
            callbackError);
        activationSpan.fail(QString::fromStdString(callbackError));
        setError(error, callbackError);
        return false;
    }

    struct MappingBatch final
    {
        VkCore *core = nullptr;
        std::vector<MappingDefinition> definitions;
    };
    struct InstalledMappingBatch final
    {
        VkCore *core = nullptr;
        std::vector<MappingId> ids;
    };
    const auto revokeInstalledMappings = [](
        std::vector<InstalledMappingBatch> &batches) {
        for (InstalledMappingBatch &batch : batches) {
            if (batch.core != nullptr) {
                (void)batch.core->removeMappings(batch.ids);
            }
        }
        batches.clear();
    };

    struct UserCommandBatch final
    {
        VkCore *core = nullptr;
        std::vector<UserCommandDefinition> definitions;
    };
    struct InstalledUserCommandBatch final
    {
        VkCore *core = nullptr;
        std::vector<
            VkUserCommandRegistry::ContributionLease> leases;
    };
    const auto revokeInstalledUserCommands =
        [activationOwnerId](
            std::vector<InstalledUserCommandBatch> &batches) {
            for (InstalledUserCommandBatch &batch : batches) {
                if (batch.core != nullptr) {
                    batch.core->userCommands().revokeOwner(
                        activationOwnerId);
                }
                batch.leases.clear();
            }
            batches.clear();
        };

    std::vector<VkCommandRegistry::CommandRegistration>
        registrations = context.takeRegistrations();
    for (auto &registration : registrations) {
        auto handler = std::move(registration.handler);
        registration.handler =
            [activationLive,
             handler = std::move(handler)](
                const CommandInvocation &invocation) {
                if (!activationLive->load(
                        std::memory_order_acquire)) {
                    return CommandExecutionResult::failure(
                        "plugin activation generation is not ready");
                }
                return handler(invocation);
            };
    }

    std::vector<ActivationContext::StagedMapping>
        stagedMappings = context.takeMappings();
    std::vector<MappingBatch> mappingBatches;
    for (ActivationContext::StagedMapping &mapping :
         stagedMappings) {
        auto batch = std::ranges::find_if(
            mappingBatches,
            [&mapping](const MappingBatch &candidate) {
                return candidate.core == mapping.core;
            });
        if (batch == mappingBatches.end()) {
            mappingBatches.push_back(
                MappingBatch{mapping.core, {}});
            batch = std::prev(mappingBatches.end());
        }
        batch->definitions.push_back(
            std::move(mapping.definition));
    }
    std::vector<UserCommandBatch> userCommandBatches;
    for (ActivationContext::StagedUserCommand &command :
         context.takeUserCommands()) {
        auto batch = std::ranges::find_if(
            userCommandBatches,
            [&command](const UserCommandBatch &candidate) {
                return candidate.core == command.core;
            });
        if (batch == userCommandBatches.end()) {
            userCommandBatches.push_back(
                UserCommandBatch{command.core, {}});
            batch = std::prev(userCommandBatches.end());
        }
        batch->definitions.push_back(
            std::move(command.definition));
    }
    std::vector<ActivationContext::Cleanup> cleanups =
        context.takeCleanups();

    std::string registrationError;
    std::vector<VkCommandRegistry::ContributionLease>
        contributions;
    auto installedMappings =
        std::make_shared<
            std::vector<InstalledMappingBatch>>();
    installedMappings->reserve(mappingBatches.size());
    auto installedUserCommands = std::make_shared<
        std::vector<InstalledUserCommandBatch>>();
    installedUserCommands->reserve(userCommandBatches.size());

    bool publicationSucceeded = true;
    std::unique_lock publicationLock(m_publicationMutex);
    if (!registrations.empty()) {
        contributions = m_commands.registerCommands(
            activationOwnerId,
            std::move(registrations),
            &registrationError);
        publicationSucceeded = !contributions.empty();
    }
    if (publicationSucceeded) {
        for (MappingBatch &batch : mappingBatches) {
            if (batch.core == nullptr) {
                registrationError =
                    "plugin '" + id
                    + "' staged a mapping without a core";
                publicationSucceeded = false;
                break;
            }
            std::vector<MappingId> mappingIds =
                batch.core->addMappings(
                    batch.definitions,
                    &registrationError);
            if (mappingIds.size()
                != batch.definitions.size()) {
                publicationSucceeded = false;
                break;
            }
            installedMappings->push_back(
                {batch.core, std::move(mappingIds)});
        }
    }
    if (publicationSucceeded) {
        for (UserCommandBatch &batch : userCommandBatches) {
            if (batch.core == nullptr) {
                registrationError =
                    "plugin '" + id
                    + "' staged a user command without a core";
                publicationSucceeded = false;
                break;
            }
            auto leases = batch.core->userCommands()
                              .registerCommands(
                                  activationOwnerId,
                                  std::move(batch.definitions),
                                  &registrationError);
            if (leases.empty()) {
                publicationSucceeded = false;
                break;
            }
            installedUserCommands->push_back(
                {batch.core, std::move(leases)});
        }
    }

    if (!publicationSucceeded) {
        activationLive->store(
            false, std::memory_order_release);
        revokeInstalledUserCommands(*installedUserCommands);
        revokeInstalledMappings(*installedMappings);
        m_commands.revokeOwner(activationOwnerId);
        contributions.clear();
        publicationLock.unlock();
        static_cast<void>(runCleanups(cleanups));
        const std::string failure = registrationError.empty()
            ? "plugin contribution publication failed"
            : registrationError;
        markActivationFailed(
            id,
            activationGeneration,
            elapsed(),
            failure);
        activationSpan.fail(QString::fromStdString(failure));
        setError(error, failure);
        return false;
    }

    std::vector<ActivationContext::Cleanup>
        publicationCleanups;
    if (!installedMappings->empty()) {
        publicationCleanups.push_back(
            [installedMappings, revokeInstalledMappings] {
                revokeInstalledMappings(*installedMappings);
            });
    }
    if (!installedUserCommands->empty()) {
        publicationCleanups.push_back(
            [installedUserCommands,
             revokeInstalledUserCommands] {
                revokeInstalledUserCommands(
                    *installedUserCommands);
            });
    }
    const std::chrono::nanoseconds duration = elapsed();
    bool generationCommitted = false;
    {
        const std::scoped_lock lock(m_mutex);
        Runtime &runtime = *m_plugins.at(id);
        if (runtime.state != PluginState::Activating
            || runtime.publicationGeneration
                != activationGeneration
            || runtime.publicationOwner
                != activationOwnerId) {
            activationLive->store(
                false, std::memory_order_release);
            registrationError =
                "plugin activation generation was replaced during publication";
        } else {
            runtime.contributions = std::move(contributions);
            runtime.publicationCleanups =
                std::move(publicationCleanups);
            runtime.cleanups = std::move(cleanups);
            runtime.activationDuration = duration;
            runtime.state = PluginState::Ready;
            runtime.activationThread = {};
            runtime.lastError.clear();
            ++runtime.successfulActivations;
            // Direct registry users may observe a staged handler before the
            // manager state commit. It remains failure-closed until this exact
            // generation is Ready.
            activationLive->store(
                true, std::memory_order_release);
            generationCommitted = true;
        }
    }
    if (!generationCommitted) {
        activationLive->store(
            false, std::memory_order_release);
        revokeInstalledUserCommands(*installedUserCommands);
        revokeInstalledMappings(*installedMappings);
        m_commands.revokeOwner(activationOwnerId);
        contributions.clear();
        publicationLock.unlock();
        static_cast<void>(runCleanups(cleanups));
        markActivationFailed(
            id,
            activationGeneration,
            duration,
            registrationError);
        activationSpan.fail(QString::fromStdString(registrationError));
        setError(error, registrationError);
        return false;
    }
    publicationLock.unlock();
    m_stateChanged.notify_all();
    if (error != nullptr) {
        error->clear();
    }
    activationSpan.annotate(
        QStringLiteral("duration_ns"),
        static_cast<qint64>(duration.count()));
    return true;
}

void VkCorePluginManager::markActivationFailed(
    const PluginId &id,
    const std::uint64_t generation,
    const std::chrono::nanoseconds duration,
    std::string message)
{
    std::string owner;
    std::shared_ptr<std::atomic_bool> live;
    std::vector<VkCommandRegistry::ContributionLease>
        contributions;
    std::vector<ActivationContext::Cleanup>
        publicationCleanups;
    std::vector<ActivationContext::Cleanup> cleanups;
    {
        const std::scoped_lock lock(m_mutex);
        Runtime &runtime = *m_plugins.at(id);
        if (runtime.publicationGeneration != generation) {
            return;
        }
        if (runtime.publicationLive) {
            runtime.publicationLive->store(
                false, std::memory_order_release);
        }
        owner = std::move(runtime.publicationOwner);
        live = std::move(runtime.publicationLive);
        contributions = std::move(runtime.contributions);
        publicationCleanups =
            std::move(runtime.publicationCleanups);
        cleanups = std::move(runtime.cleanups);
        runtime.activationDuration = duration;
        runtime.state = PluginState::Failed;
        runtime.activationThread = {};
        runtime.lastError = std::move(message);
    }
    // A stale failure can only revoke the exact generation owner. Host-owned
    // cleanup is serialized; plugin cleanup may re-enter the manager and runs
    // after the publication lock has been released.
    {
        const std::scoped_lock publicationLock(
            m_publicationMutex);
        if (!owner.empty()) {
            m_commands.revokeOwner(owner);
        }
        contributions.clear();
        static_cast<void>(runCleanups(publicationCleanups));
    }
    live.reset();
    static_cast<void>(runCleanups(cleanups));
    m_stateChanged.notify_all();
}

PluginInfo VkCorePluginManager::makeInfo(
    const Runtime &runtime) const
{
    return PluginInfo{
        runtime.spec.id,
        runtime.spec.displayName,
        runtime.spec.version,
        runtime.state,
        runtime.enabled,
        runtime.spec.lazy,
        runtime.spec.builtin,
        runtime.spec.canDisable,
        runtime.spec.userVisible,
        runtime.activationAttempts,
        runtime.successfulActivations,
        runtime.activationDuration,
        runtime.activationReason,
        runtime.lastError};
}

} // namespace vkui::vk
