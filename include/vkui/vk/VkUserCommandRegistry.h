#pragma once

#include "VkCommandRegistry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vkui::vk {

enum class UserCommandNargs : std::uint8_t
{
    Zero,
    One,
    ZeroOrOne,
    Any,
    OneOrMore
};

/** Declarative Ex alias targeting one semantic command id. */
struct UserCommandDefinition final
{
    UserCommandDefinition() = default;

    UserCommandDefinition(
        std::string commandName,
        CommandId commandTarget,
        std::string commandDescription,
        const UserCommandNargs argumentCount = UserCommandNargs::Zero,
        const bool bang = false,
        const bool range = false,
        const bool count = false,
        std::optional<BufferId> localBuffer = std::nullopt)
        : name(std::move(commandName))
        , target(std::move(commandTarget))
        , description(std::move(commandDescription))
        , nargs(argumentCount)
        , acceptsBang(bang)
        , acceptsRange(range)
        , acceptsCount(count)
        , buffer(localBuffer)
    {
    }

    std::string name;
    CommandId target;
    std::string description;
    UserCommandNargs nargs = UserCommandNargs::Zero;
    bool acceptsBang = false;
    bool acceptsRange = false;
    bool acceptsCount = false;
    std::optional<BufferId> buffer;
};

struct UserCommandRequest final
{
    std::string name;
    std::vector<std::string> arguments;
    std::string rawArguments;
    bool bang = false;
    std::optional<CommandLineRange> range;
    std::optional<std::size_t> count;
    CommandInvocation context;
};

/** Detached resolution; the Ex parser can emit invocation directly. */
struct ResolvedUserCommand final
{
    CommandInvocation invocation;
    UserCommandDefinition definition;
};

/**
 * Thread-safe owner-scoped registry for Neovim-style user commands.
 *
 * Names are aliases only; executable behavior remains in VkCommandRegistry.
 * Buffer-local aliases win over global aliases, and leases prevent a stale
 * generation from revoking a newer registration with the same name.
 */
class VkUserCommandRegistry final
{
private:
    struct SharedState;

public:
    class ContributionLease final
    {
    public:
        ContributionLease() noexcept = default;
        ~ContributionLease();
        ContributionLease(ContributionLease &&other) noexcept;
        ContributionLease &operator=(
            ContributionLease &&other) noexcept;
        ContributionLease(const ContributionLease &) = delete;
        ContributionLease &operator=(
            const ContributionLease &) = delete;

        void reset() noexcept;
        [[nodiscard]] bool valid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

    private:
        friend class VkUserCommandRegistry;
        ContributionLease(
            std::weak_ptr<SharedState> state,
            std::uint64_t token,
            std::string owner,
            std::string name,
            std::optional<BufferId> buffer);

        std::weak_ptr<SharedState> m_state;
        std::uint64_t m_token = 0;
        std::string m_owner;
        std::string m_name;
        std::optional<BufferId> m_buffer;
    };

    VkUserCommandRegistry();
    ~VkUserCommandRegistry();
    VkUserCommandRegistry(const VkUserCommandRegistry &) = delete;
    VkUserCommandRegistry &operator=(
        const VkUserCommandRegistry &) = delete;

    [[nodiscard]] ContributionLease registerCommand(
        std::string owner,
        UserCommandDefinition definition,
        std::string *error = nullptr);
    [[nodiscard]] std::vector<ContributionLease> registerCommands(
        std::string owner,
        std::vector<UserCommandDefinition> definitions,
        std::string *error = nullptr);

    [[nodiscard]] std::optional<ResolvedUserCommand> resolve(
        UserCommandRequest request,
        std::string *error = nullptr) const;
    [[nodiscard]] bool contains(
        std::string_view name,
        std::optional<BufferId> buffer = std::nullopt) const;
    [[nodiscard]] std::optional<UserCommandDefinition> definition(
        std::string_view name,
        std::optional<BufferId> buffer = std::nullopt) const;
    std::size_t revokeOwner(std::string_view owner) noexcept;
    /** Removes aliases scoped to a buffer that no longer exists. */
    std::size_t revokeBuffer(BufferId buffer) noexcept;
    /** Removes aliases for a destroyed buffer batch under one lock. */
    std::size_t revokeBuffers(
        std::span<const BufferId> buffers) noexcept;

    [[nodiscard]] static bool isValidName(
        std::string_view name) noexcept;
    [[nodiscard]] static bool validatesArgumentCount(
        UserCommandNargs nargs,
        std::size_t count) noexcept;

private:
    std::shared_ptr<SharedState> m_state;
};

} // namespace vkui::vk
