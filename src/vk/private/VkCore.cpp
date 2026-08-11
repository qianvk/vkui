#include "VkCoreInternal.h"

namespace vkui::vk {

VkCore::VkCore()
    : m_impl(std::make_unique<Implementation>())
{
}

VkCore::~VkCore() = default;

void VkCore::setEnabled(const bool enabled)
{
    if (m_impl->baseMode == Mode::Insert) {
        m_impl->finishInsertRepeat();
    } else if (m_impl->baseMode == Mode::Replace) {
        m_impl->finishReplaceMode(
            nullptr, m_impl->activeView, false);
    }
    m_impl->closeInsertUndoBlock();
    m_impl->enabled = enabled;
    m_impl->recordingMacro.reset();
    m_impl->macroPlaybackActive = false;
    m_impl->macroStepsRemaining = 0;
    m_impl->promptInput.reset();
    m_impl->baseMode = Mode::Normal;
    m_impl->clearPendingState();
    if (enabled) {
        for (auto &[viewId, view] : m_impl->views) {
            static_cast<void>(viewId);
            view.selectionAnchorOffset.reset();
        }
    }
}

bool VkCore::isEnabled() const noexcept
{
    return m_impl->enabled;
}

std::optional<Mode> VkCore::mode() const noexcept
{
    return m_impl->enabled
        ? std::optional<Mode>(m_impl->effectiveMode())
        : std::nullopt;
}

void VkCore::setVirtualEditMode(
    const VirtualEditMode mode)
{
    if (m_impl->virtualEdit == mode) {
        return;
    }
    m_impl->virtualEdit = mode;
    if (mode != VirtualEditMode::None) {
        return;
    }

    // Option changes are immediate, like :set virtualedit=. Remove coladd
    // state that is no longer representable instead of leaking it into the
    // next command or host projection.
    for (auto &[window, view] : m_impl->views) {
        static_cast<void>(window);
        const auto buffer = m_impl->buffers.find(view.buffer);
        const auto cursor = view.cursors.find(view.buffer);
        if (buffer == m_impl->buffers.end()
            || cursor == view.cursors.end()) {
            continue;
        }
        const DisplayPosition current =
            m_impl->currentDisplayPosition(
                view, buffer->second);
        const std::size_t bounded = std::min(
            current.column,
            m_impl->displayLine(
                buffer->second,
                current.buffer.line)
                .width);
        m_impl->setDisplayPosition(
            view,
            m_impl->displayPositionForColumn(
                buffer->second,
                current.buffer.line,
                bounded));
        if (view.preferredColumn) {
            view.preferredColumn = std::min(
                *view.preferredColumn, bounded);
        }
    }
    if (m_impl->visualSelection
        && m_impl->visualSelection->kind
            == VisualKind::Block) {
        auto buffer = m_impl->buffers.find(
            m_impl->visualSelection->buffer);
        if (buffer != m_impl->buffers.end()) {
            const std::size_t bounded = std::min(
                m_impl->visualSelection
                    ->anchorDisplayColumn,
                m_impl->displayLine(
                    buffer->second,
                    m_impl->visualSelection->anchor.line)
                    .width);
            const DisplayPosition anchor =
                m_impl->displayPositionForColumn(
                    buffer->second,
                    m_impl->visualSelection->anchor.line,
                    bounded);
            m_impl->visualSelection->anchor =
                anchor.buffer;
            m_impl->visualSelection
                ->anchorDisplayColumn = anchor.column;
        }
    }
}

VirtualEditMode VkCore::virtualEditMode() const noexcept
{
    return m_impl->virtualEdit;
}

} // namespace vkui::vk
