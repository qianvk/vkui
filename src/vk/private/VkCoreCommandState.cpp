#include "VkCoreInternal.h"

namespace vkui::vk {

void VkCore::Implementation::emitHost(
    DispatchResult &result,
    const HostAction action,
    const std::size_t count,
    const bool countWasExplicit) const
{
    Event event;
    event.type = EventType::HostAction;
    event.hostAction = action;
    event.view = activeView;
    event.count = std::max<std::size_t>(1, count);
    event.countWasExplicit = countWasExplicit;
    result.events.push_back(std::move(event));
}

void VkCore::Implementation::emitModeIfChanged(
    DispatchResult &result,
    const Mode before)
{
    const Mode after = effectiveMode();
    if (before == after) {
        return;
    }
    Event event;
    event.type = EventType::ModeChanged;
    event.mode = after;
    event.view = activeView;
    result.events.push_back(std::move(event));
}

void VkCore::Implementation::consumeMappedHostAction(
    DispatchResult &result,
    const HostAction action)
{
    // A mapped host action is a complete command. Grammar accumulated by
    // the preceding command must not leak into the next physical key.
    const Mode before = effectiveMode();
    pendingOperator.reset();
    normalCount = 0;
    prefix = CommandPrefix::None;
    emitModeIfChanged(result, before);
    emitHost(result, action);
}

void VkCore::Implementation::consumeSemanticCommand(
    DispatchResult &result,
    std::string command,
    const InputTargetId inputTarget)
{
    // A semantic command has the same grammar boundary as a mapped host
    // action, but its implementation is resolved by the Core Plugin
    // registry rather than by a physical panel position.
    const Mode before = effectiveMode();
    const std::size_t commandCount =
        std::max<std::size_t>(normalCount, 1);
    const bool countWasExplicit = normalCount != 0;
    const auto active = views.find(activeView);
    const BufferId commandBuffer =
        active == views.end() ? 0 : active->second.buffer;
    pendingOperator.reset();
    pendingArgument.reset();
    normalCount = 0;
    prefix = CommandPrefix::None;
    emitModeIfChanged(result, before);

    Event event;
    event.type = EventType::CommandRequested;
    event.view = activeView;
    event.inputTarget = inputTarget;
    event.buffer = commandBuffer;
    event.mode = before;
    event.commandId = std::move(command);
    event.count = commandCount;
    event.countWasExplicit = countWasExplicit;
    result.events.push_back(std::move(event));
}

[[nodiscard]] bool VkCore::Implementation::containsHostBoundaryEvent(
    const DispatchResult &result,
    const std::size_t firstEvent) noexcept
{
    return std::any_of(
        result.events.cbegin()
            + static_cast<std::ptrdiff_t>(firstEvent),
        result.events.cend(),
        [](const Event &event) {
            return event.type == EventType::HostAction
                || event.type
                    == EventType::CommandRequested
                || event.type
                    == EventType::CommandLineRequested
                || event.type
                    == EventType::BufferActivationRequested
                || event.type
                    == EventType::DisplayMotionRequested;
        });
}

void VkCore::Implementation::beginHostBarrier(DispatchResult &result)
{
    pendingGeneration.reset();
    pendingView = 0;
    pendingTarget = 0;
    pendingHintPolicy.reset();
    pendingHostBarrierGeneration =
        ++hostBarrierGeneration;
    result.hostBarrierGeneration =
        pendingHostBarrierGeneration;
}

void VkCore::Implementation::emitCursor(
    DispatchResult &result,
    const ViewId viewId,
    const View &view) const
{
    if (view.buffer == 0) {
        return;
    }
    const auto cursor = view.cursors.find(view.buffer);
    if (cursor == view.cursors.cend()) {
        return;
    }
    Event event;
    event.type = EventType::CursorChanged;
    event.view = viewId;
    event.buffer = view.buffer;
    event.cursor = cursor->second;
    result.events.push_back(std::move(event));
}

void VkCore::Implementation::emitViewport(
    DispatchResult &result,
    const WindowId windowId,
    const View &view) const
{
    Event event;
    event.type = EventType::ViewportChanged;
    event.view = windowId;
    event.buffer = view.buffer;
    event.topline = view.topline;
    event.leftColumn = view.leftColumn;
    event.viewportRows = view.viewportRows;
    event.viewportColumns = view.viewportColumns;
    event.scrollRows = view.scrollRows;
    result.events.push_back(std::move(event));
}

[[nodiscard]] std::vector<VkCore::Implementation::WindowCursorState>
VkCore::Implementation::captureWindowCursors(const BufferId bufferId) const
{
    std::vector<WindowCursorState> state;
    state.reserve(views.size());
    for (const auto &[windowId, view] : views) {
        if (view.buffer != bufferId) {
            continue;
        }
        const auto cursor = view.cursors.find(bufferId);
        state.push_back(
            WindowCursorState{
                windowId,
                cursor == view.cursors.cend()
                    ? Cursor{}
                    : cursor->second,
                view.selectionAnchorOffset});
    }
    return state;
}

void VkCore::Implementation::restoreWindowCursors(
    const BufferId bufferId,
    const std::vector<WindowCursorState> &state)
{
    const auto foundBuffer = buffers.find(bufferId);
    if (foundBuffer == buffers.end()) {
        return;
    }
    for (const WindowCursorState &saved : state) {
        const auto foundView = views.find(saved.window);
        if (foundView == views.end()
            || foundView->second.buffer != bufferId) {
            continue;
        }
        foundView->second.cursors[bufferId] =
            clampCursor(
                foundBuffer->second,
                saved.cursor,
                !enabled);
        foundView->second.selectionAnchorOffset =
            saved.selectionAnchorOffset
            ? std::optional<std::size_t>(std::min(
                  *saved.selectionAnchorOffset,
                  foundBuffer->second.text().size()))
            : std::nullopt;
        foundView->second.preferredColumn.reset();
    }
}

void VkCore::Implementation::emitAttachedCursors(
    DispatchResult &result,
    const BufferId bufferId) const
{
    for (const auto &[windowId, view] : views) {
        if (view.buffer == bufferId) {
            emitCursor(result, windowId, view);
        }
    }
}

} // namespace vkui::vk
