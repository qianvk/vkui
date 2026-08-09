#pragma once

#include "VkTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vkui::vk {

using CommandId = std::string;
using CommandOwnerId = std::string;

struct CommandInvocation final
{
    CommandInvocation() = default;

    explicit CommandInvocation(
        CommandId command,
        std::vector<std::string> commandArguments = {},
        const std::size_t commandCount = 1,
        const bool explicitCount = false,
        const Mode commandMode = Mode::Normal,
        const WindowId commandWindow = 0,
        const BufferId commandBuffer = 0,
        const InputTargetId target = 0,
        const bool commandBang = false,
        std::optional<CommandLineRange> range = std::nullopt,
        std::string argumentsText = {})
        : id(std::move(command))
        , arguments(std::move(commandArguments))
        , count(commandCount)
        , countWasExplicit(explicitCount)
        , mode(commandMode)
        , window(commandWindow)
        , buffer(commandBuffer)
        , inputTarget(target)
        , bang(commandBang)
        , lineRange(std::move(range))
        , rawArguments(std::move(argumentsText))
    {
    }

    CommandId id;
    std::vector<std::string> arguments;

    /**
     * Immutable execution context captured when VK resolved the command.
     *
     * This is deliberately semantic state rather than a Qt key event.  It
     * gives commands the same count/window/buffer context that Neovim makes
     * available to command callbacks while keeping the input engine as the
     * sole owner of grammar and typeahead.
     */
    std::size_t count = 1;
    bool countWasExplicit = false;
    Mode mode = Mode::Normal;
    WindowId window = 0;
    BufferId buffer = 0;
    InputTargetId inputTarget = 0;
    bool bang = false;
    std::optional<CommandLineRange> lineRange;
    std::string rawArguments;
};

struct CommandExecutionResult final
{
    bool succeeded = false;
    std::string error;

    [[nodiscard]] static CommandExecutionResult success();
    [[nodiscard]] static CommandExecutionResult failure(
        std::string message);
    [[nodiscard]] explicit operator bool() const noexcept;
};

struct CommandDescriptor final
{
    CommandId id;
    std::string description;
};

/**
 * Thread-safe registry for stable, semantic commands.
 *
 * A registration is owned by exactly one plugin. The returned lease is the
 * only lifetime token for that contribution: destroying or resetting it
 * removes precisely the matching registration while holding the registry
 * mutex. The monotonically increasing token prevents an old lease from
 * removing a newer command that happens to reuse the same id.
 */
class VkCommandRegistry final
{
private:
    struct SharedState;

public:
    using Handler = std::function<
        CommandExecutionResult(const CommandInvocation &)>;

    struct CommandRegistration final
    {
        CommandDescriptor descriptor;
        Handler handler;
    };

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
        [[nodiscard]] const CommandOwnerId &owner() const noexcept;
        [[nodiscard]] const CommandId &commandId() const noexcept;

    private:
        friend class VkCommandRegistry;

        ContributionLease(
            std::weak_ptr<SharedState> state,
            std::uint64_t token,
            CommandOwnerId owner,
            CommandId command);

        std::weak_ptr<SharedState> m_state;
        std::uint64_t m_token = 0;
        CommandOwnerId m_owner;
        CommandId m_command;
    };

    VkCommandRegistry();
    ~VkCommandRegistry();

    VkCommandRegistry(const VkCommandRegistry &) = delete;
    VkCommandRegistry &operator=(const VkCommandRegistry &) = delete;
    VkCommandRegistry(VkCommandRegistry &&) = delete;
    VkCommandRegistry &operator=(VkCommandRegistry &&) = delete;

    /**
     * Atomically publishes one command and returns its lifetime lease.
     *
     * An invalid lease indicates validation or id-conflict failure. When
     * supplied, error receives a stable diagnostic suitable for logs or UI.
     */
    [[nodiscard]] ContributionLease registerCommand(
        CommandOwnerId owner,
        CommandDescriptor descriptor,
        Handler handler,
        std::string *error = nullptr);

    /**
     * Atomically publishes a group of commands.
     *
     * Either every command becomes visible under one registry lock or none
     * does. This lets plugin activation stage contributions and commit them
     * only after the activation callback has succeeded.
     */
    [[nodiscard]] std::vector<ContributionLease> registerCommands(
        CommandOwnerId owner,
        std::vector<CommandRegistration> registrations,
        std::string *error = nullptr);

    [[nodiscard]] CommandExecutionResult execute(
        const CommandInvocation &invocation) const;
    [[nodiscard]] CommandExecutionResult execute(
        std::string_view id,
        std::vector<std::string> arguments = {}) const;

    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] std::optional<CommandDescriptor> descriptor(
        std::string_view id) const;
    [[nodiscard]] std::optional<CommandOwnerId> ownerOf(
        std::string_view id) const;
    [[nodiscard]] std::vector<CommandId> commandIdsForOwner(
        std::string_view owner) const;
    [[nodiscard]] std::size_t size() const noexcept;

    /**
     * Revokes all contributions for exactly one owner under one lock.
     *
     * Existing leases become inert. A handler already copied by an in-flight
     * execute call may finish, but no later lookup can begin that command.
     */
    std::size_t revokeOwner(std::string_view owner) noexcept;

    [[nodiscard]] static bool isValidCommandId(
        std::string_view id) noexcept;

private:
    std::shared_ptr<SharedState> m_state;
};

} // namespace vkui::vk
