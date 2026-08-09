#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace vkui::vk {

/**
 * Renderer-independent window-layout operations understood by VkCore.
 *
 * The enum is deliberately about window semantics, never Qt widgets or a
 * physical three-column layout.  Core Plugins can therefore route the same
 * command to editor, explorer, settings, or future SDK-provided windows.
 */
enum class WindowOperation : std::uint8_t
{
    FocusLeft,
    FocusDown,
    FocusUp,
    FocusRight,
    FocusNext,
    FocusPrevious,
    FocusFirst,
    FocusLast,
    FocusPreviouslyAccessed,
    MoveFarLeft,
    MoveFarBottom,
    MoveFarTop,
    MoveFarRight,
    SplitHorizontal,
    SplitVertical,
    Close,
    Quit,
    CloseOthers,
    Equalize,
    IncreaseHeight,
    DecreaseHeight,
    DecreaseWidth,
    IncreaseWidth,
    MaximizeWidth,
    MaximizeHeight,
    ExchangeNext,
    RotateForward,
    RotateBackward
};

/** One executable child of Neovim's native CTRL-W grammar. */
struct WindowCommandDescriptor final
{
    char32_t key = U'\0';
    std::u32string_view notation;
    std::string_view commandId;
    std::string_view description;
    WindowOperation operation = WindowOperation::FocusLeft;
    int order = 0;
};

inline constexpr char WindowFocusLeftCommand[] =
    "vkery.window.focus-left";
inline constexpr char WindowFocusDownCommand[] =
    "vkery.window.focus-down";
inline constexpr char WindowFocusUpCommand[] =
    "vkery.window.focus-up";
inline constexpr char WindowFocusRightCommand[] =
    "vkery.window.focus-right";
inline constexpr char WindowFocusNextCommand[] =
    "vkery.window.focus-next";
inline constexpr char WindowFocusPreviousCommand[] =
    "vkery.window.focus-previous";
inline constexpr char WindowFocusFirstCommand[] =
    "vkery.window.focus-first";
inline constexpr char WindowFocusLastCommand[] =
    "vkery.window.focus-last";
inline constexpr char WindowFocusPreviouslyAccessedCommand[] =
    "vkery.window.focus-previously-accessed";
inline constexpr char WindowMoveFarLeftCommand[] =
    "vkery.window.move-far-left";
inline constexpr char WindowMoveFarBottomCommand[] =
    "vkery.window.move-far-bottom";
inline constexpr char WindowMoveFarTopCommand[] =
    "vkery.window.move-far-top";
inline constexpr char WindowMoveFarRightCommand[] =
    "vkery.window.move-far-right";
inline constexpr char WindowSplitHorizontalCommand[] =
    "vkery.window.split-horizontal";
inline constexpr char WindowSplitVerticalCommand[] =
    "vkery.window.split-vertical";
inline constexpr char WindowCloseCommand[] =
    "vkery.window.close";
inline constexpr char WindowQuitCommand[] =
    "vkery.window.quit";
inline constexpr char WindowCloseOthersCommand[] =
    "vkery.window.close-others";
inline constexpr char WindowEqualizeCommand[] =
    "vkery.window.equalize";
inline constexpr char WindowIncreaseHeightCommand[] =
    "vkery.window.increase-height";
inline constexpr char WindowDecreaseHeightCommand[] =
    "vkery.window.decrease-height";
inline constexpr char WindowDecreaseWidthCommand[] =
    "vkery.window.decrease-width";
inline constexpr char WindowIncreaseWidthCommand[] =
    "vkery.window.increase-width";
inline constexpr char WindowMaximizeWidthCommand[] =
    "vkery.window.maximize-width";
inline constexpr char WindowMaximizeHeightCommand[] =
    "vkery.window.maximize-height";
inline constexpr char WindowExchangeNextCommand[] =
    "vkery.window.exchange-next";
inline constexpr char WindowRotateForwardCommand[] =
    "vkery.window.rotate-forward";
inline constexpr char WindowRotateBackwardCommand[] =
    "vkery.window.rotate-backward";

/**
 * Canonical CTRL-W command table shared by execution and input discovery.
 *
 * Only operations with an authoritative cross-renderer host transaction are
 * present. Neovim-compatible keys whose panel-layout mutation is not yet
 * transactional stay out of both command registration and which-key hints;
 * advertising a command that deterministically fails is worse than an
 * explicit unsupported-input error.
 */
inline constexpr std::array WindowCommands{
    WindowCommandDescriptor{
        U'h', U"h", WindowFocusLeftCommand,
        "Go to the left window", WindowOperation::FocusLeft, 10},
    WindowCommandDescriptor{
        U'j', U"j", WindowFocusDownCommand,
        "Go to the window below", WindowOperation::FocusDown, 20},
    WindowCommandDescriptor{
        U'k', U"k", WindowFocusUpCommand,
        "Go to the window above", WindowOperation::FocusUp, 30},
    WindowCommandDescriptor{
        U'l', U"l", WindowFocusRightCommand,
        "Go to the right window", WindowOperation::FocusRight, 40},
    WindowCommandDescriptor{
        U'w', U"w", WindowFocusNextCommand,
        "Go to the next window", WindowOperation::FocusNext, 50},
    WindowCommandDescriptor{
        U'W', U"W", WindowFocusPreviousCommand,
        "Go to the previous window", WindowOperation::FocusPrevious, 60},
    WindowCommandDescriptor{
        U't', U"t", WindowFocusFirstCommand,
        "Go to the top-left window", WindowOperation::FocusFirst, 70},
    WindowCommandDescriptor{
        U'b', U"b", WindowFocusLastCommand,
        "Go to the bottom-right window", WindowOperation::FocusLast, 80},
    WindowCommandDescriptor{
        U'p', U"p", WindowFocusPreviouslyAccessedCommand,
        "Go to the previously accessed window",
        WindowOperation::FocusPreviouslyAccessed, 90},
    WindowCommandDescriptor{
        U's', U"s", WindowSplitHorizontalCommand,
        "Split window horizontally", WindowOperation::SplitHorizontal, 140},
    WindowCommandDescriptor{
        U'v', U"v", WindowSplitVerticalCommand,
        "Split window vertically", WindowOperation::SplitVertical, 150},
    WindowCommandDescriptor{
        U'c', U"c", WindowCloseCommand,
        "Close the current window", WindowOperation::Close, 160},
    WindowCommandDescriptor{
        U'q', U"q", WindowQuitCommand,
        "Quit the current window", WindowOperation::Quit, 170},
    WindowCommandDescriptor{
        U'=', U"=", WindowEqualizeCommand,
        "Make windows equally high and wide", WindowOperation::Equalize, 190},
    WindowCommandDescriptor{
        U'+', U"+", WindowIncreaseHeightCommand,
        "Resize panel down", WindowOperation::IncreaseHeight, 200},
    WindowCommandDescriptor{
        U'-', U"-", WindowDecreaseHeightCommand,
        "Resize panel up", WindowOperation::DecreaseHeight, 210},
    WindowCommandDescriptor{
        U'<', U"<", WindowDecreaseWidthCommand,
        "Resize panel left", WindowOperation::DecreaseWidth, 220},
    WindowCommandDescriptor{
        U'>', U">", WindowIncreaseWidthCommand,
        "Resize panel right", WindowOperation::IncreaseWidth, 230},
    WindowCommandDescriptor{
        U'|', U"|", WindowMaximizeWidthCommand,
        "Maximize window width", WindowOperation::MaximizeWidth, 240},
    WindowCommandDescriptor{
        U'_', U"_", WindowMaximizeHeightCommand,
        "Maximize window height", WindowOperation::MaximizeHeight, 250},
};

[[nodiscard]] constexpr const WindowCommandDescriptor *
windowCommand(const std::string_view id) noexcept
{
    for (const WindowCommandDescriptor &descriptor : WindowCommands) {
        if (descriptor.commandId == id) {
            return &descriptor;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr const WindowCommandDescriptor *
windowCommand(const WindowOperation operation) noexcept
{
    for (const WindowCommandDescriptor &descriptor : WindowCommands) {
        if (descriptor.operation == operation) {
            return &descriptor;
        }
    }
    return nullptr;
}

} // namespace vkui::vk
