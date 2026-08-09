#pragma once

#include <vkui/vk/VkTypes.h>

#include <QPointer>
#include <QString>
#include <QStringView>
#include <QVector>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

class QObject;
class QWidget;

namespace vkui::vk::widgets {

enum class VkPanelEdge : std::uint8_t
{
    Left,
    Center,
    Right,
    Floating
};

/**
 * Semantic identity and host operations for one mounted panel.
 *
 * Plugin commands target (providerId, surfaceId), never a splitter index.
 * The WindowId is the matching VkCore viewport, while QWidget remains a
 * replaceable renderer owned by the host layout.
 */
struct VkPanelDescriptor final
{
    using NavigateCallback =
        std::function<bool(int horizontal, int vertical)>;

    VkPanelDescriptor() = default;

    VkPanelDescriptor(
        vkui::vk::WindowId windowValue,
        QString providerIdValue,
        QString surfaceIdValue,
        QString titleValue,
        QPointer<QWidget> widgetValue,
        VkPanelEdge preferredEdgeValue,
        std::function<bool()> isVisibleValue,
        std::function<bool(bool)> setVisibleValue,
        std::function<bool()> focusValue,
        NavigateCallback navigateValue = {})
        : window(windowValue)
        , providerId(std::move(providerIdValue))
        , surfaceId(std::move(surfaceIdValue))
        , title(std::move(titleValue))
        , widget(std::move(widgetValue))
        , preferredEdge(preferredEdgeValue)
        , isVisible(std::move(isVisibleValue))
        , setVisible(std::move(setVisibleValue))
        , focus(std::move(focusValue))
        , navigate(std::move(navigateValue))
    {
    }

    vkui::vk::WindowId window = 0;
    QString providerId;
    QString surfaceId;
    QString title;
    QPointer<QWidget> widget;
    VkPanelEdge preferredEdge = VkPanelEdge::Center;
    std::function<bool()> isVisible;
    std::function<bool(bool)> setVisible;
    /**
     * Requests focus and reports whether the request was accepted.
     *
     * This is intentionally not an immediate hasFocus() observation: native
     * window systems may deliver FocusIn asynchronously after the semantic
     * window transaction has committed.
     */
    std::function<bool()> focus;
    /**
     * Applies one semantic navigation step inside this panel.
     *
     * Components are normalized direction signs. A panel decides whether a
     * horizontal step opens hierarchy/pages and whether a vertical step moves
     * selection; MainWindow never needs to know the renderer type.
     */
    NavigateCallback navigate;
};

class VkPanelRegistry;

/**
 * Move-only registration token.
 *
 * Destroying a plugin activation's leases removes only that activation's
 * panels, so disabling one Core Plugin cannot invalidate another plugin.
 */
class VkPanelLease final
{
public:
    VkPanelLease() = default;
    ~VkPanelLease();

    VkPanelLease(VkPanelLease &&other) noexcept;
    VkPanelLease &operator=(VkPanelLease &&other) noexcept;

    VkPanelLease(const VkPanelLease &) = delete;
    VkPanelLease &operator=(const VkPanelLease &) = delete;

    void reset();
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    friend class VkPanelRegistry;
    struct State;

    explicit VkPanelLease(
        std::weak_ptr<State> state,
        std::uint64_t registration,
        std::shared_ptr<std::atomic_bool> active);

    std::weak_ptr<State> m_state;
    std::uint64_t m_registration = 0;
    std::shared_ptr<std::atomic_bool> m_active;
};

/**
 * GUI-thread registry connecting VkCore windows to host panel renderers.
 */
class VkPanelRegistry final
{
public:
    VkPanelRegistry();
    ~VkPanelRegistry();

    VkPanelRegistry(const VkPanelRegistry &) = delete;
    VkPanelRegistry &operator=(const VkPanelRegistry &) = delete;

    [[nodiscard]] VkPanelLease registerPanel(
        VkPanelDescriptor descriptor,
        QString *error = nullptr);

    /**
     * Replaces the renderer projected by an existing semantic panel.
     *
     * The WindowId and registration lease remain unchanged. This is the
     * panel equivalent of attaching another buffer to a VkCore window and is
     * required when a surviving split is promoted into a persistent host
     * container. The operation is synchronous and transactional on the GUI
     * thread: failure leaves the old descriptor registered.
     */
    [[nodiscard]] bool rebindPanel(
        vkui::vk::WindowId window,
        VkPanelDescriptor descriptor,
        QString *error = nullptr);

    [[nodiscard]] std::optional<VkPanelDescriptor> panel(
        QStringView providerId,
        QStringView surfaceId) const;
    [[nodiscard]] std::optional<VkPanelDescriptor> panel(
        vkui::vk::WindowId window) const;
    [[nodiscard]] std::optional<VkPanelDescriptor> panelFor(
        const QObject *object) const;
    [[nodiscard]] QVector<VkPanelDescriptor> panels() const;

    [[nodiscard]] bool toggle(
        QStringView providerId,
        QStringView surfaceId);
    /**
     * Applies an explicit visibility state to a semantic panel.
     *
     * Unlike toggle(), this operation is idempotent. Commands named "close"
     * must never reveal an already hidden panel when replayed.
     */
    [[nodiscard]] bool setVisible(
        QStringView providerId,
        QStringView surfaceId,
        bool visible);
    [[nodiscard]] bool focus(
        QStringView providerId,
        QStringView surfaceId);
    [[nodiscard]] bool focusWindow(vkui::vk::WindowId window);
    [[nodiscard]] bool navigateWindow(
        vkui::vk::WindowId window,
        int horizontalDirection,
        int verticalDirection);

    /**
     * Resolves the nearest visible panel from actual screen geometry.
     *
     * This remains correct after panels are reordered or dynamically
     * inserted; no default three-column ordinal enters the decision.
     */
    [[nodiscard]] std::optional<vkui::vk::WindowId> adjacentWindow(
        vkui::vk::WindowId origin,
        int horizontalDirection,
        int verticalDirection = 0) const;

private:
    std::shared_ptr<VkPanelLease::State> m_state;
};

} // namespace vkui::vk::widgets
