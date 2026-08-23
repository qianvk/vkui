// SPDX-License-Identifier: MIT

#pragma once

#include <QPointer>
#include <QVector>
#include <QtGui/qwindowdefs.h>
#include <memory>
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#include <optional>
#endif
#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <vkui/window/VSystemButton.h>

class QEvent;
class QWidget;
class QWindow;

namespace vkui::windowing {

class VPlatformWindowBackend;

/**
 * Platform-independent state and event coordinator for one QWidget window.
 */
class VNativeWindowController final : public QObject {
  public:
    explicit VNativeWindowController(QWidget& host);
    ~VNativeWindowController() override;

    VNativeWindowController(const VNativeWindowController&) = delete;
    VNativeWindowController& operator=(const VNativeWindowController&) = delete;

    [[nodiscard]] bool isReady() const noexcept;

    [[nodiscard]] QList<QWidget*> titleBars() const;
    [[nodiscard]] bool addTitleBar(QWidget* titleBar);
    [[nodiscard]] bool removeTitleBar(QWidget* titleBar);
    void clearTitleBars();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    [[nodiscard]] std::optional<QPoint> trafficLightOrigin() const noexcept;
    [[nodiscard]] bool setTrafficLightOrigin(const QPoint& origin);
#endif

    [[nodiscard]] bool isHitTestVisible(const QWidget* widget) const;
    [[nodiscard]] bool setHitTestVisible(QWidget* widget, bool visible);

    [[nodiscard]] bool isResizable() const noexcept;
    [[nodiscard]] bool setResizable(bool resizable);
    [[nodiscard]] VSystemButtons systemButtons() const noexcept;
    [[nodiscard]] bool setSystemButtons(VSystemButtons buttons);
    [[nodiscard]] bool systemButtonsVisible() const noexcept;
    [[nodiscard]] bool setSystemButtonsVisible(bool visible);

    void showSystemMenu(const QPoint& globalPosition);
    void centralize();
    void raiseWindow();

    // Platform backend view of the controller state.
    [[nodiscard]] QWidget* host() const noexcept;
    [[nodiscard]] QWindow* window() const noexcept;
    [[nodiscard]] Qt::WindowFlags windowFlags() const;
    [[nodiscard]] bool hostSizeFixed() const;
    [[nodiscard]] bool pointIsDraggable(const QPoint& windowPosition) const;
    void synchronizeNativeWindow();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    enum class MoveState {
        Idle,
        Armed,
        Moving,
    };

    [[nodiscard]] bool belongsToHost(const QWidget* widget) const noexcept;
    [[nodiscard]] QRect geometryInHost(const QWidget* widget) const;
    void refreshNativeHandle();
    void detachNativeHandle() noexcept;
    void applyBackendState();
    void pruneNullWidgets();

    QPointer<QWidget> host_;
    QPointer<QWindow> window_;
    Qt::WindowFlags applicationWindowFlags_;
    WId nativeId_ = 0;
    std::unique_ptr<VPlatformWindowBackend> backend_;
    QVector<QPointer<QWidget>> titleBars_;
    QVector<QPointer<QWidget>> hitTestVisibleWidgets_;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    std::optional<QPoint> trafficLightOrigin_;
#endif
    VSystemButtons systemButtons_ = VStandardSystemButtons;
    bool systemButtonsVisible_ = true;
    MoveState moveState_ = MoveState::Idle;
    bool resizable_ = true;
    bool refreshingNativeHandle_ = false;
    bool backendAttached_ = false;
};

} // namespace vkui::windowing
