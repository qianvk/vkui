#include "VkUserCommandRegistry.h"

#include <algorithm>
#include <array>
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

struct CommandKey final
{
    std::string name;
    std::optional<BufferId> buffer;

    friend bool operator==(
        const CommandKey &,
        const CommandKey &) = default;
};

struct CommandKeyHash final
{
    std::size_t operator()(const CommandKey &key) const noexcept
    {
        const std::size_t name = std::hash<std::string>{}(key.name);
        const std::size_t buffer = key.buffer
            ? std::hash<BufferId>{}(*key.buffer)
            : 0;
        return name ^ (buffer + 0x9e3779b9U + (name << 6U)
                       + (name >> 2U));
    }
};

} // namespace

struct VkUserCommandRegistry::SharedState final
{
    struct Record final
    {
        std::uint64_t token = 0;
        std::string owner;
        UserCommandDefinition definition;
    };

    mutable std::mutex mutex;
    std::unordered_map<CommandKey, Record, CommandKeyHash> commands;
    std::uint64_t nextToken = 1;
};

VkUserCommandRegistry::ContributionLease::ContributionLease(
    std::weak_ptr<SharedState> state,
    const std::uint64_t token,
    std::string owner,
    std::string name,
    const std::optional<BufferId> buffer)
    : m_state(std::move(state))
    , m_token(token)
    , m_owner(std::move(owner))
    , m_name(std::move(name))
    , m_buffer(buffer)
{
}

VkUserCommandRegistry::ContributionLease::~ContributionLease()
{
    reset();
}

VkUserCommandRegistry::ContributionLease::ContributionLease(
    ContributionLease &&other) noexcept
    : m_state(std::move(other.m_state))
    , m_token(std::exchange(other.m_token, 0))
    , m_owner(std::move(other.m_owner))
    , m_name(std::move(other.m_name))
    , m_buffer(std::exchange(other.m_buffer, std::nullopt))
{
}

VkUserCommandRegistry::ContributionLease &
VkUserCommandRegistry::ContributionLease::operator=(
    ContributionLease &&other) noexcept
{
    if (this != &other) {
        reset();
        m_state = std::move(other.m_state);
        m_token = std::exchange(other.m_token, 0);
        m_owner = std::move(other.m_owner);
        m_name = std::move(other.m_name);
        m_buffer = std::exchange(other.m_buffer, std::nullopt);
    }
    return *this;
}

void VkUserCommandRegistry::ContributionLease::reset() noexcept
{
    if (m_token == 0) {
        return;
    }
    if (const auto state = m_state.lock()) {
        const std::scoped_lock lock(state->mutex);
        const auto found = state->commands.find(
            CommandKey{m_name, m_buffer});
        if (found != state->commands.end()
            && found->second.token == m_token
            && found->second.owner == m_owner) {
            state->commands.erase(found);
        }
    }
    m_token = 0;
    m_state.reset();
}

bool VkUserCommandRegistry::ContributionLease::valid() const noexcept
{
    const auto state = m_state.lock();
    if (!state || m_token == 0) {
        return false;
    }
    const std::scoped_lock lock(state->mutex);
    const auto found = state->commands.find(
        CommandKey{m_name, m_buffer});
    return found != state->commands.end()
        && found->second.token == m_token
        && found->second.owner == m_owner;
}

VkUserCommandRegistry::ContributionLease::operator bool() const noexcept
{
    return valid();
}

VkUserCommandRegistry::VkUserCommandRegistry()
    : m_state(std::make_shared<SharedState>())
{
}

VkUserCommandRegistry::~VkUserCommandRegistry() = default;

VkUserCommandRegistry::ContributionLease
VkUserCommandRegistry::registerCommand(
    std::string owner,
    UserCommandDefinition definition,
    std::string *const error)
{
    std::vector<UserCommandDefinition> definitions;
    definitions.push_back(std::move(definition));
    auto leases = registerCommands(
        std::move(owner), std::move(definitions), error);
    if (leases.empty()) {
        return {};
    }
    return std::move(leases.front());
}

std::vector<VkUserCommandRegistry::ContributionLease>
VkUserCommandRegistry::registerCommands(
    std::string owner,
    std::vector<UserCommandDefinition> definitions,
    std::string *const error)
{
    if (owner.empty() || definitions.empty()) {
        setError(error, owner.empty()
            ? "user-command owner must not be empty"
            : "user-command batch must not be empty");
        return {};
    }
    std::unordered_set<CommandKey, CommandKeyHash> batch;
    for (const UserCommandDefinition &definition : definitions) {
        const CommandKey key{definition.name, definition.buffer};
        if (!isValidName(definition.name)
            || !VkCommandRegistry::isValidCommandId(
                definition.target)
            || definition.description.empty()
            || !batch.insert(key).second) {
            setError(
                error,
                "invalid or duplicate user command '"
                    + definition.name + "'");
            return {};
        }
    }

    const auto state = m_state;
    const std::scoped_lock lock(state->mutex);
    for (const auto &key : batch) {
        const auto existing = state->commands.find(key);
        if (existing != state->commands.end()) {
            setError(
                error,
                "user command '" + key.name
                    + "' is already owned by plugin '"
                    + existing->second.owner + "'");
            return {};
        }
    }
    if (state->nextToken
        > std::numeric_limits<std::uint64_t>::max()
            - definitions.size()) {
        setError(error, "user-command token space is exhausted");
        return {};
    }

    std::vector<ContributionLease> leases;
    leases.reserve(definitions.size());
    for (UserCommandDefinition &definition : definitions) {
        const std::uint64_t token = state->nextToken++;
        const CommandKey key{definition.name, definition.buffer};
        const std::string name = definition.name;
        const auto buffer = definition.buffer;
        state->commands.emplace(
            key,
            SharedState::Record{
                token, owner, std::move(definition)});
        leases.push_back(ContributionLease(
            state, token, owner, name, buffer));
    }
    if (error != nullptr) {
        error->clear();
    }
    return leases;
}

std::optional<ResolvedUserCommand>
VkUserCommandRegistry::resolve(
    UserCommandRequest request,
    std::string *const error) const
{
    auto resolvedDefinition = definition(
        request.name,
        request.context.buffer == 0
            ? std::nullopt
            : std::optional<BufferId>(
                  request.context.buffer));
    if (!resolvedDefinition) {
        setError(
            error,
            "user command '" + request.name
                + "' is not registered");
        return std::nullopt;
    }
    UserCommandDefinition definition =
        std::move(*resolvedDefinition);

    if (!validatesArgumentCount(
            definition.nargs, request.arguments.size())) {
        setError(
            error,
            "user command '" + request.name
                + "' received an invalid number of arguments");
        return std::nullopt;
    }
    if (request.bang && !definition.acceptsBang) {
        setError(error, "user command does not accept !");
        return std::nullopt;
    }
    if (request.range && (!definition.acceptsRange
                          || request.range->firstLine
                              > request.range->lastLine)) {
        setError(error, "user command does not accept this range");
        return std::nullopt;
    }
    if (request.count && !definition.acceptsCount) {
        setError(error, "user command does not accept a count");
        return std::nullopt;
    }

    request.context.id = definition.target;
    request.context.arguments = std::move(request.arguments);
    request.context.bang = request.bang;
    request.context.lineRange = request.range;
    request.context.rawArguments = std::move(request.rawArguments);
    if (request.count) {
        request.context.count = std::max<std::size_t>(1, *request.count);
        request.context.countWasExplicit = true;
    }
    if (error != nullptr) {
        error->clear();
    }
    return ResolvedUserCommand{
        std::move(request.context), std::move(definition)};
}

bool VkUserCommandRegistry::contains(
    const std::string_view name,
    const std::optional<BufferId> buffer) const
{
    const auto state = m_state;
    const std::scoped_lock lock(state->mutex);
    if (buffer && state->commands.contains(
                      CommandKey{std::string(name), buffer})) {
        return true;
    }
    return state->commands.contains(
        CommandKey{std::string(name), std::nullopt});
}

std::optional<UserCommandDefinition>
VkUserCommandRegistry::definition(
    const std::string_view name,
    const std::optional<BufferId> buffer) const
{
    const auto state = m_state;
    const std::scoped_lock lock(state->mutex);
    if (buffer) {
        const auto local = state->commands.find(
            CommandKey{std::string(name), buffer});
        if (local != state->commands.end()) {
            return local->second.definition;
        }
    }
    const auto global = state->commands.find(
        CommandKey{std::string(name), std::nullopt});
    return global == state->commands.end()
        ? std::nullopt
        : std::optional<UserCommandDefinition>(
              global->second.definition);
}

std::size_t VkUserCommandRegistry::revokeOwner(
    const std::string_view owner) noexcept
{
    const auto state = m_state;
    const std::scoped_lock lock(state->mutex);
    std::size_t removed = 0;
    for (auto command = state->commands.begin();
         command != state->commands.end();) {
        if (command->second.owner == owner) {
            command = state->commands.erase(command);
            ++removed;
        } else {
            ++command;
        }
    }
    return removed;
}

std::size_t VkUserCommandRegistry::revokeBuffer(
    const BufferId buffer) noexcept
{
    const std::array buffers{buffer};
    return revokeBuffers(buffers);
}

std::size_t VkUserCommandRegistry::revokeBuffers(
    const std::span<const BufferId> buffers) noexcept
{
    if (buffers.empty()) {
        return 0;
    }
    const auto state = m_state;
    const std::scoped_lock lock(state->mutex);
    std::size_t removedCount = 0;
    for (auto command = state->commands.begin();
         command != state->commands.end();) {
        if (command->first.buffer
            && std::ranges::find(
                   buffers, *command->first.buffer)
                != buffers.end()) {
            command = state->commands.erase(command);
            ++removedCount;
        } else {
            ++command;
        }
    }
    return removedCount;
}

bool VkUserCommandRegistry::isValidName(
    const std::string_view name) noexcept
{
    if (name.empty() || name.size() > 128
        || name.front() < 'A' || name.front() > 'Z') {
        return false;
    }
    return std::all_of(
        name.cbegin() + 1,
        name.cend(),
        [](const char value) {
            return (value >= 'a' && value <= 'z')
                || (value >= 'A' && value <= 'Z')
                || (value >= '0' && value <= '9');
        });
}

bool VkUserCommandRegistry::validatesArgumentCount(
    const UserCommandNargs nargs,
    const std::size_t count) noexcept
{
    switch (nargs) {
    case UserCommandNargs::Zero:
        return count == 0;
    case UserCommandNargs::One:
        return count == 1;
    case UserCommandNargs::ZeroOrOne:
        return count <= 1;
    case UserCommandNargs::Any:
        return true;
    case UserCommandNargs::OneOrMore:
        return count >= 1;
    }
    return false;
}

} // namespace vkui::vk
