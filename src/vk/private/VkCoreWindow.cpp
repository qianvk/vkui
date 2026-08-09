#include "VkCoreInternal.h"

#include <vkui/core/VkDiagnostics.h>

namespace vkui::vk {

ViewId VkCore::registerView(const ViewKind kind)
{
    const ViewId id = m_impl->nextView++;
    Implementation::View view;
    view.kind = kind;
    m_impl->views.emplace(id, std::move(view));
    if (m_impl->activeView == 0) {
        m_impl->activeView = id;
    }
    vkui::writeDiagnostic(
        vkui::DiagnosticLevel::Info,
        QStringLiteral("vkcore.window"),
        QStringLiteral("register"),
        QStringLiteral("VK window registered"),
        QJsonObject{
            {QStringLiteral("window"), static_cast<qint64>(id)},
            {QStringLiteral("kind"), static_cast<int>(kind)},
        });
    return id;
}

void VkCore::unregisterView(const ViewId view)
{
    const bool existed = m_impl->views.contains(view);
    const bool replacedActive =
        m_impl->activeView == view;
    const bool ownsDisplayMotion =
        m_impl->pendingDisplayMotion
        && m_impl->pendingDisplayMotion->window == view;
    if (replacedActive
        && m_impl->baseMode == Mode::Replace) {
        m_impl->finishReplaceMode(nullptr, view, false);
    }
    if (m_impl->visualSelection
        && m_impl->visualSelection->window == view) {
        // A Visual anchor is window-local. Never leave modal state pointing
        // at an identity that no longer exists.
        m_impl->leaveVisualMode();
    }
    if (m_impl->promptInput
        && m_impl->promptInput->window == view) {
        m_impl->promptInput.reset();
    }
    if (replacedActive) {
        m_impl->closeInsertUndoBlock();
    }
    static_cast<void>(
        m_impl->input.removeWindowMappings(view));
    m_impl->views.erase(view);
    if (m_impl->previousActiveView == view) {
        m_impl->previousActiveView = 0;
    }
    if (replacedActive) {
        m_impl->activeView = m_impl->views.empty()
            ? 0
            : m_impl->views.cbegin()->first;
        m_impl->continueInsertUndoBlock(
            m_impl->activeView);
    }
    if (replacedActive || ownsDisplayMotion) {
        cancelPendingInput();
    }
    if (existed) {
        vkui::writeDiagnostic(
            vkui::DiagnosticLevel::Info,
            QStringLiteral("vkcore.window"),
            QStringLiteral("unregister"),
            QStringLiteral("VK window unregistered"),
            QJsonObject{
                {QStringLiteral("window"), static_cast<qint64>(view)},
                {QStringLiteral("new_active"),
                 static_cast<qint64>(m_impl->activeView)},
            });
    }
}

bool VkCore::setActiveView(const ViewId view)
{
    if (!m_impl->views.contains(view)) {
        return false;
    }
    if (m_impl->activeView != view) {
        if (m_impl->baseMode == Mode::Replace) {
            m_impl->finishReplaceMode(
                nullptr, m_impl->activeView, false);
        }
        m_impl->closeInsertUndoBlock();
        m_impl->previousActiveView =
            m_impl->activeView;
        m_impl->activeView = view;
        if (m_impl->baseMode == Mode::Visual) {
            const auto selection =
                m_impl->visualSelection;
            const auto selectedView =
                m_impl->views.find(view);
            if (!selection
                || selectedView == m_impl->views.end()
                || selection->window != view
                || selection->buffer
                    != selectedView->second.buffer) {
                // Like Neovim, a window switch does not retarget a Visual
                // anchor to whichever buffer happens to receive focus.
                m_impl->leaveVisualMode();
            }
        }
        m_impl->continueInsertUndoBlock(view);
        // The host may be applying the action that produced the current
        // barrier. Its acknowledgement supplies the authoritative resulting
        // context; changing the visible view must not discard the queued RHS.
        if (!m_impl->pendingHostBarrierGeneration) {
            cancelPendingInput();
        }
        vkui::writeDiagnostic(
            vkui::DiagnosticLevel::Debug,
            QStringLiteral("vkcore.window"),
            QStringLiteral("focus"),
            QStringLiteral("Active VK window changed"),
            QJsonObject{
                {QStringLiteral("window"), static_cast<qint64>(view)},
                {QStringLiteral("previous"),
                 static_cast<qint64>(m_impl->previousActiveView)},
            });
    }
    return true;
}

ViewId VkCore::activeView() const noexcept
{
    return m_impl->activeView;
}

WindowId VkCore::previousWindow() const noexcept
{
    return m_impl->views.contains(
               m_impl->previousActiveView)
        ? m_impl->previousActiveView
        : WindowId{0};
}

ViewKind VkCore::viewKind(const ViewId view) const noexcept
{
    const auto found = m_impl->views.find(view);
    return found == m_impl->views.end()
        ? ViewKind::Other
        : found->second.kind;
}

bool VkCore::setWindowViewport(
    const WindowId id,
    const std::size_t topline,
    const std::size_t leftColumn,
    const std::size_t viewportRows,
    const std::optional<std::size_t> scrollRows,
    const std::optional<std::size_t> viewportColumns)
{
    const auto found = m_impl->views.find(id);
    if (found == m_impl->views.end()) {
        return false;
    }
    auto &window = found->second;
    window.topline = topline;
    window.leftColumn = leftColumn;
    window.viewportRows = viewportRows;
    if (viewportColumns) {
        window.viewportColumns = *viewportColumns;
    }
    if (scrollRows) {
        window.scrollRows = *scrollRows;
    }
    const auto buffer =
        m_impl->buffers.find(window.buffer);
    if (buffer != m_impl->buffers.end()) {
        window.topline = std::min(
            window.topline,
            buffer->second.lineStarts.size() - 1);
    }
    return true;
}

bool VkCore::setWindowViewportMotionAuthority(
    const WindowId id,
    const ViewportMotionAuthority authority)
{
    const auto found = m_impl->views.find(id);
    if (found == m_impl->views.end()) {
        return false;
    }
    found->second.viewportMotionAuthority = authority;
    return true;
}

std::optional<WindowSnapshot> VkCore::window(
    const WindowId id) const
{
    const auto found = m_impl->views.find(id);
    if (found == m_impl->views.end()) {
        return std::nullopt;
    }
    const auto cursor =
        found->second.cursors.find(
            found->second.buffer);
    const bool ownsVisualSelection =
        m_impl->baseMode == Mode::Visual
        && m_impl->visualSelection.has_value()
        && m_impl->visualSelection->window == id
        && m_impl->visualSelection->buffer != 0
        && m_impl->visualSelection->buffer
            == found->second.buffer;
    const auto buffer = m_impl->buffers.find(
        found->second.buffer);
    const Cursor bufferCursor =
        cursor == found->second.cursors.cend()
        ? Cursor{}
        : cursor->second;
    const DisplayPosition displayCursor =
        buffer == m_impl->buffers.cend()
        ? DisplayPosition{bufferCursor, bufferCursor.column}
        : m_impl->currentDisplayPosition(
              found->second, buffer->second);
    const std::optional<DisplayPosition> displayAnchor =
        ownsVisualSelection
        ? std::optional<DisplayPosition>(
              DisplayPosition{
                  m_impl->visualSelection->anchor,
                  m_impl->visualSelection
                      ->anchorDisplayColumn})
        : std::nullopt;
    const std::optional<VisualBlockRange> blockRange =
        ownsVisualSelection
            && m_impl->visualSelection->kind
                == VisualKind::Block
            && buffer != m_impl->buffers.cend()
        ? m_impl->currentVisualBlockRange(
              id, found->second, buffer->second)
        : std::nullopt;
    return WindowSnapshot{
        id,
        found->second.kind,
        found->second.buffer,
        bufferCursor,
        found->second.topline,
        found->second.leftColumn,
        found->second.viewportRows,
        found->second.viewportColumns,
        found->second.scrollRows,
        found->second.viewportMotionAuthority,
        ownsVisualSelection
            ? std::optional<Cursor>(
                  m_impl->visualSelection->anchor)
            : std::nullopt,
        ownsVisualSelection
            && m_impl->visualSelection->kind
                == VisualKind::Line,
        ownsVisualSelection
            && m_impl->visualSelection->kind
                == VisualKind::Block,
        displayCursor,
        displayAnchor,
        blockRange,
        buffer == m_impl->buffers.cend()
            ? std::size_t{8}
            : buffer->second.tabStop};
}

bool VkCore::cloneWindowNavigationState(
    const WindowId source,
    const WindowId target)
{
    const auto sourceView = m_impl->views.find(source);
    const auto targetView = m_impl->views.find(target);
    if (sourceView == m_impl->views.end()
        || targetView == m_impl->views.end()
        || source == target) {
        return false;
    }
    targetView->second.jumpList =
        sourceView->second.jumpList;
    targetView->second.jumpIndex =
        sourceView->second.jumpIndex;
    if (sourceView->second.buffer
        == targetView->second.buffer) {
        const auto buffer = m_impl->buffers.find(
            targetView->second.buffer);
        targetView->second.changeIndex =
            buffer == m_impl->buffers.end()
            ? 0
            : std::min(
                  sourceView->second.changeIndex,
                  buffer->second.changeList.size());
    }
    targetView->second.alternateBuffer =
        sourceView->second.alternateBuffer;
    targetView->second.previousContext =
        sourceView->second.previousContext;
    return true;
}

std::vector<VisualBlockRow> VkCore::visualBlockRows(
    const WindowId id,
    const std::size_t firstLine,
    const std::size_t lastLine) const
{
    const auto found = m_impl->views.find(id);
    if (found == m_impl->views.cend()) {
        return {};
    }
    const auto buffer = m_impl->buffers.find(
        found->second.buffer);
    if (buffer == m_impl->buffers.cend()) {
        return {};
    }
    const auto range = m_impl->currentVisualBlockRange(
        id, found->second, buffer->second);
    if (!range || firstLine > lastLine) {
        return {};
    }
    const std::size_t boundedFirst = std::max(
        range->firstLine, firstLine);
    const std::size_t boundedLast = std::min(
        {range->lastLine,
         lastLine,
         buffer->second.lineStarts.size() - 1});
    if (boundedFirst > boundedLast) {
        return {};
    }
    std::vector<VisualBlockRow> rows;
    rows.reserve(boundedLast - boundedFirst + 1);
    for (std::size_t line = boundedFirst;
         line <= boundedLast;
         ++line) {
        rows.push_back(m_impl->visualBlockRow(
            buffer->second, line, *range));
    }
    return rows;
}

} // namespace vkui::vk
