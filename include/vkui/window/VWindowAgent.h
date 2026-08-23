// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <memory>
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#include <optional>
#endif
#include <vkui/VkUiGlobal.h>
#include <vkui/window/VSystemButton.h>

class QWidget;

namespace vkui {

class VWindowAgentPrivate;

/**
 * Native full-content behavior composed into one top-level QWidget.
 *
 * The host must outlive the agent. Prefer declaring the host first and the
 * agent last, or declaring the agent as the host window's final data member.
 * Native system buttons remain owned, rendered, and hit-tested by the platform.
 */
class VKUI_WINDOW_EXPORT VWindowAgent final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool resizable READ isResizable WRITE setResizable NOTIFY resizableChanged)
    Q_PROPERTY(vkui::VSystemButtons systemButtons READ systemButtons WRITE setSystemButtons NOTIFY
                   systemButtonsChanged)
    Q_PROPERTY(bool systemButtonsVisible READ systemButtonsVisible WRITE setSystemButtonsVisible
                   NOTIFY systemButtonsVisibleChanged)

  public:
    explicit VWindowAgent(QWidget& window);
    ~VWindowAgent() override;

    VWindowAgent(const VWindowAgent&) = delete;
    VWindowAgent& operator=(const VWindowAgent&) = delete;

    [[nodiscard]] QList<QWidget*> titleBars() const;
    [[nodiscard]] bool addTitleBar(QWidget* titleBar);
    [[nodiscard]] bool removeTitleBar(QWidget* titleBar);
    void clearTitleBars();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    /** Requested top-left origin of the native traffic-light group. */
    [[nodiscard]] std::optional<QPoint> trafficLightOrigin() const noexcept;
    void setTrafficLightOrigin(const QPoint& origin);
#endif

    [[nodiscard]] bool isHitTestVisible(const QWidget* widget) const;
    [[nodiscard]] bool setHitTestVisible(QWidget* widget, bool visible = true);

    [[nodiscard]] bool isResizable() const noexcept;
    void setResizable(bool resizable);

    /** Returns which platform-owned buttons are configured for the host. */
    [[nodiscard]] VSystemButtons systemButtons() const noexcept;
    /** Configures the platform-owned buttons without replacing their native implementation. */
    void setSystemButtons(VSystemButtons buttons);

    [[nodiscard]] bool systemButtonsVisible() const noexcept;
    /** Temporarily shows or hides the complete configured native button set. */
    void setSystemButtonsVisible(bool visible);

  public slots:
    void showSystemMenu(const QPoint& position);
    void centralize();
    void raiseWindow();

  signals:
    void resizableChanged(bool resizable);
    void systemButtonsChanged(vkui::VSystemButtons buttons);
    void systemButtonsVisibleChanged(bool visible);

  private:
    std::unique_ptr<VWindowAgentPrivate> d_;
};

} // namespace vkui
