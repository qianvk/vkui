#include "VkCoreInternal.h"

namespace vkui::vk {

void VkCore::Implementation::clearPendingState()
{
    input.clear();
    inputHintSession.reset();
    keywordCompletion.reset();
    if (insertOneNormalPending) {
        insertOneNormalPending = false;
        insertOneNormalSawKey = false;
        if (baseMode == Mode::Normal) {
            baseMode = Mode::Insert;
        }
    }
    prefix = CommandPrefix::None;
    normalCount = 0;
    pendingOperator.reset();
    pendingArgument.reset();
    pendingGeneration.reset();
    pendingView = 0;
    pendingTarget = 0;
    pendingHintPolicy.reset();
    pendingHostBarrierGeneration.reset();
    pendingLocationMove.reset();
    pendingDisplayMotion.reset();
    macroPlaybackActive = false;
    macroStepsRemaining = 0;
    ++inputGeneration;
}

[[nodiscard]] bool VkCore::Implementation::bufferAttached(
    const BufferId buffer) const noexcept
{
    return std::ranges::any_of(
        views,
        [buffer](const auto &entry) {
            return entry.second.buffer == buffer;
        });
}

std::size_t VkCore::Implementation::eraseBuffers(
    const std::unordered_set<BufferId> &removed)
{
    if (removed.empty()) {
        return 0;
    }
    std::vector<BufferId> destroyed;
    destroyed.reserve(removed.size());
    for (const BufferId id : removed) {
        if (buffers.contains(id)) {
            destroyed.push_back(id);
        }
    }
    if (destroyed.empty()) {
        return 0;
    }
    // Buffer identities are capabilities. Revoke every local input
    // contribution before erasing the identities so a recycled id can
    // never inherit a stale mapping or Ex alias. Each subsystem handles
    // the whole batch with one scan (and one trie rebuild for mappings).
    static_cast<void>(
        input.removeBufferMappings(destroyed));
    static_cast<void>(
        userCommands.revokeBuffers(destroyed));
    if (insertUndoBuffer
        && removed.contains(*insertUndoBuffer)) {
        closeInsertUndoBlock();
    }
    if (replaceSession
        && removed.contains(replaceSession->buffer)) {
        replaceSession.reset();
        if (baseMode == Mode::Replace) {
            baseMode = Mode::Normal;
        }
    }
    for (const BufferId id : destroyed) {
        const auto buffer = buffers.find(id);
        paths.erase(buffer->second.path);
        buffers.erase(buffer);
    }
    for (auto &mark : globalMarks) {
        if (mark && removed.contains(mark->buffer)) {
            mark.reset();
        }
    }
    std::erase_if(
        bufferOrder,
        [&removed](const BufferId id) {
            return removed.contains(id);
        });
    for (auto &[id, view] : views) {
        static_cast<void>(id);
        std::erase_if(
            view.jumpList,
            [&removed](const Location &location) {
                return removed.contains(location.buffer);
            });
        view.jumpIndex = view.jumpList.empty()
            ? 0
            : std::min(
                  view.jumpIndex,
                  view.jumpList.size() - 1);
        if (view.previousContext
            && removed.contains(
                view.previousContext->buffer)) {
            view.previousContext.reset();
        }
        if (removed.contains(view.alternateBuffer)) {
            view.alternateBuffer = 0;
        }
        for (const BufferId buffer : removed) {
            view.cursors.erase(buffer);
        }
        if (removed.contains(view.buffer)) {
            view.buffer = 0;
            view.changeIndex = 0;
            view.preferredColumn.reset();
        }
    }
    clearPendingState();
    inputContextView = 0;
    inputContextTarget = 0;
    return destroyed.size();
}

} // namespace vkui::vk
