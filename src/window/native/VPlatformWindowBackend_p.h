// SPDX-License-Identifier: MIT

#pragma once

#include <QPoint>
#include <QtGui/qwindowdefs.h>
#include <memory>

class QWidget;
class QWindow;

namespace vkui::windowing {

class VNativeWindowController;

/**
 * Typed platform boundary for one top-level native window.
 *
 * A backend never owns the QWidget or QWindow. The controller detaches the
 * backend before either native handle can be replaced or destroyed.
 */
class VPlatformWindowBackend {
  public:
    explicit VPlatformWindowBackend(VNativeWindowController& controller) noexcept
        : controller_(controller) {}
    virtual ~VPlatformWindowBackend() = default;

    VPlatformWindowBackend(const VPlatformWindowBackend&) = delete;
    VPlatformWindowBackend& operator=(const VPlatformWindowBackend&) = delete;

    [[nodiscard]] virtual bool attach(QWidget* host, QWindow* window, WId nativeId) = 0;
    // Detaching only releases backend resources. The host owns its native
    // handle, so mutating that handle during teardown would be unsafe.
    virtual void detach() noexcept = 0;
    virtual void synchronize() = 0;

    virtual void configureSystemButtons() = 0;
    virtual void updateSystemButtonLayout() = 0;
    virtual void applySystemButtonState() = 0;
    virtual void updateResizablePolicy() = 0;

    virtual void prepareSystemMove() {}
    virtual void startSystemMove(const QPoint& globalPosition) = 0;
    virtual void cancelSystemMove() {}
    virtual void handleTitleBarDoubleClick(const QPoint& globalPosition) = 0;
    virtual void showSystemMenu(const QPoint& globalPosition) = 0;
    [[nodiscard]] virtual bool centralize() {
        return false;
    }
    virtual void raiseWindow() = 0;

  protected:
    VNativeWindowController& controller_;
};

[[nodiscard]] std::unique_ptr<VPlatformWindowBackend>
createPlatformWindowBackend(VNativeWindowController& controller);

} // namespace vkui::windowing
