#include "VkCommandRegistry.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vkui::vk {

namespace {

void setError(std::string *const target, std::string message)
{
    if (target != nullptr) {
        *target = std::move(message);
    }
}

[[nodiscard]] bool isCommandCharacter(const char value) noexcept
{
    return (value >= 'a' && value <= 'z')
        || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9')
        || value == '.'
        || value == '_'
        || value == '-'
        || value == ':';
}

} // namespace

struct VkCommandRegistry::SharedState final
{
    struct Record final
    {
        std::uint64_t token = 0;
        CommandOwnerId owner;
        CommandDescriptor descriptor;
        Handler handler;
    };

    mutable std::mutex mutex;
    std::unordered_map<CommandId, Record> commands;
    std::uint64_t nextToken = 1;
};

CommandExecutionResult CommandExecutionResult::success()
{
    return CommandExecutionResult{true, {}};
}

CommandExecutionResult CommandExecutionResult::failure(
    std::string message)
{
    return CommandExecutionResult{false, std::move(message)};
}

CommandExecutionResult::operator bool() const noexcept
{
    return succeeded;
}

VkCommandRegistry::ContributionLease::ContributionLease(
    std::weak_ptr<SharedState> state,
    const std::uint64_t token,
    CommandOwnerId owner,
    CommandId command)
    : m_state(std::move(state))
    , m_token(token)
    , m_owner(std::move(owner))
    , m_command(std::move(command))
{
}

VkCommandRegistry::ContributionLease::~ContributionLease()
{
    reset();
}

VkCommandRegistry::ContributionLease::ContributionLease(
    ContributionLease &&other) noexcept
    : m_state(std::move(other.m_state))
    , m_token(std::exchange(other.m_token, 0))
    , m_owner(std::move(other.m_owner))
    , m_command(std::move(other.m_command))
{
}

VkCommandRegistry::ContributionLease &
VkCommandRegistry::ContributionLease::operator=(
    ContributionLease &&other) noexcept
{
    if (this != &other) {
        reset();
        m_state = std::move(other.m_state);
        m_token = std::exchange(other.m_token, 0);
        m_owner = std::move(other.m_owner);
        m_command = std::move(other.m_command);
    }
    return *this;
}

void VkCommandRegistry::ContributionLease::reset() noexcept
{
    if (m_token == 0) {
        return;
    }

    if (const std::shared_ptr<SharedState> state = m_state.lock()) {
        const std::scoped_lock lock(state->mutex);
        const auto command = state->commands.find(m_command);
        if (command != state->commands.end()
            && command->second.token == m_token
            && command->second.owner == m_owner) {
            state->commands.erase(command);
        }
    }

    m_token = 0;
    m_state.reset();
}

bool VkCommandRegistry::ContributionLease::valid() const noexcept
{
    if (m_token == 0) {
        return false;
    }

    const std::shared_ptr<SharedState> state = m_state.lock();
    if (!state) {
        return false;
    }

    const std::scoped_lock lock(state->mutex);
    const auto command = state->commands.find(m_command);
    return command != state->commands.end()
        && command->second.token == m_token
        && command->second.owner == m_owner;
}

VkCommandRegistry::ContributionLease::operator bool() const noexcept
{
    return valid();
}

const CommandOwnerId &
VkCommandRegistry::ContributionLease::owner() const noexcept
{
    return m_owner;
}

const CommandId &
VkCommandRegistry::ContributionLease::commandId() const noexcept
{
    return m_command;
}

VkCommandRegistry::VkCommandRegistry()
    : m_state(std::make_shared<SharedState>())
{
}

VkCommandRegistry::~VkCommandRegistry() = default;

VkCommandRegistry::ContributionLease
VkCommandRegistry::registerCommand(
    CommandOwnerId owner,
    CommandDescriptor descriptor,
    Handler handler,
    std::string *const error)
{
    std::vector<CommandRegistration> registrations;
    registrations.push_back(
        CommandRegistration{
            std::move(descriptor),
            std::move(handler)});

    std::vector<ContributionLease> leases = registerCommands(
        std::move(owner),
        std::move(registrations),
        error);
    if (leases.empty()) {
        return {};
    }
    return std::move(leases.front());
}

std::vector<VkCommandRegistry::ContributionLease>
VkCommandRegistry::registerCommands(
    CommandOwnerId owner,
    std::vector<CommandRegistration> registrations,
    std::string *const error)
{
    if (owner.empty()) {
        setError(error, "command owner must not be empty");
        return {};
    }
    if (registrations.empty()) {
        if (error != nullptr) {
            error->clear();
        }
        return {};
    }

    std::unordered_set<CommandId> batchIds;
    batchIds.reserve(registrations.size());
    for (const CommandRegistration &registration : registrations) {
        if (!isValidCommandId(registration.descriptor.id)) {
            setError(
                error,
                "invalid command id '" + registration.descriptor.id + "'");
            return {};
        }
        if (!registration.handler) {
            setError(
                error,
                "command '" + registration.descriptor.id
                    + "' has no handler");
            return {};
        }
        if (!batchIds.insert(registration.descriptor.id).second) {
            setError(
                error,
                "command '" + registration.descriptor.id
                    + "' is declared more than once in one contribution");
            return {};
        }
    }

    std::vector<ContributionLease> leases;
    leases.reserve(registrations.size());

    const std::shared_ptr<SharedState> state = m_state;
    const std::scoped_lock lock(state->mutex);
    for (const CommandRegistration &registration : registrations) {
        const auto existing =
            state->commands.find(registration.descriptor.id);
        if (existing != state->commands.end()) {
            setError(
                error,
                "command '" + registration.descriptor.id
                    + "' is already owned by plugin '"
                    + existing->second.owner + "'");
            return {};
        }
    }

    if (state->nextToken
        > std::numeric_limits<std::uint64_t>::max()
            - registrations.size()) {
        setError(error, "command contribution token space is exhausted");
        return {};
    }

    std::vector<std::uint64_t> tokens;
    tokens.reserve(registrations.size());
    for (const CommandRegistration &registration : registrations) {
        const std::uint64_t token = state->nextToken++;
        tokens.push_back(token);
        leases.push_back(
            ContributionLease(
                state,
                token,
                owner,
                registration.descriptor.id));
    }

    std::vector<CommandId> inserted;
    inserted.reserve(registrations.size());
    try {
        for (std::size_t index = 0;
             index < registrations.size();
             ++index) {
            CommandRegistration &registration = registrations[index];
            const CommandId id = registration.descriptor.id;
            inserted.push_back(id);
            state->commands.emplace(
                id,
                SharedState::Record{
                    tokens[index],
                    owner,
                    std::move(registration.descriptor),
                    std::move(registration.handler)});
        }
    } catch (...) {
        for (const CommandId &id : inserted) {
            state->commands.erase(id);
        }
        throw;
    }

    if (error != nullptr) {
        error->clear();
    }
    return leases;
}

CommandExecutionResult VkCommandRegistry::execute(
    const CommandInvocation &invocation) const
{
    Handler handler;
    {
        const std::scoped_lock lock(m_state->mutex);
        const auto command = m_state->commands.find(invocation.id);
        if (command == m_state->commands.end()) {
            return CommandExecutionResult::failure(
                "command '" + invocation.id + "' is not registered");
        }
        handler = command->second.handler;
    }

    try {
        return handler(invocation);
    } catch (const std::exception &exception) {
        return CommandExecutionResult::failure(
            "command '" + invocation.id
                + "' threw an exception: " + exception.what());
    } catch (...) {
        return CommandExecutionResult::failure(
            "command '" + invocation.id
                + "' threw an unknown exception");
    }
}

CommandExecutionResult VkCommandRegistry::execute(
    const std::string_view id,
    std::vector<std::string> arguments) const
{
    return execute(
        CommandInvocation{
            std::string(id),
            std::move(arguments)});
}

bool VkCommandRegistry::contains(const std::string_view id) const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->commands.contains(std::string(id));
}

std::optional<CommandDescriptor> VkCommandRegistry::descriptor(
    const std::string_view id) const
{
    const std::scoped_lock lock(m_state->mutex);
    const auto command = m_state->commands.find(std::string(id));
    if (command == m_state->commands.end()) {
        return std::nullopt;
    }
    return command->second.descriptor;
}

std::optional<CommandOwnerId> VkCommandRegistry::ownerOf(
    const std::string_view id) const
{
    const std::scoped_lock lock(m_state->mutex);
    const auto command = m_state->commands.find(std::string(id));
    if (command == m_state->commands.end()) {
        return std::nullopt;
    }
    return command->second.owner;
}

std::vector<CommandId> VkCommandRegistry::commandIdsForOwner(
    const std::string_view owner) const
{
    std::vector<CommandId> result;
    const std::scoped_lock lock(m_state->mutex);
    for (const auto &[id, command] : m_state->commands) {
        if (command.owner == owner) {
            result.push_back(id);
        }
    }
    std::ranges::sort(result);
    return result;
}

std::size_t VkCommandRegistry::size() const noexcept
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->commands.size();
}

std::size_t VkCommandRegistry::revokeOwner(
    const std::string_view owner) noexcept
{
    std::size_t revoked = 0;
    const std::scoped_lock lock(m_state->mutex);
    for (auto command = m_state->commands.begin();
         command != m_state->commands.end();) {
        if (command->second.owner == owner) {
            command = m_state->commands.erase(command);
            ++revoked;
        } else {
            ++command;
        }
    }
    return revoked;
}

bool VkCommandRegistry::isValidCommandId(
    const std::string_view id) noexcept
{
    return !id.empty()
        && std::ranges::all_of(id, isCommandCharacter);
}

} // namespace vkui::vk
