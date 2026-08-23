// SPDX-License-Identifier: MIT

#include "native/VNativeWindowController_p.h"

#include <QWidget>
#include <vkui/window/VWindowAgent.h>

namespace vkui {

class VWindowAgentPrivate final {
  public:
    explicit VWindowAgentPrivate(QWidget& window) : controller(window) {}

    windowing::VNativeWindowController controller;
};

VWindowAgent::VWindowAgent(QWidget& window)
    : QObject(nullptr), d_(std::make_unique<VWindowAgentPrivate>(window)) {
    Q_ASSERT_X(window.isWindow(), "VWindowAgent", "The host must be a top-level QWidget");
}

VWindowAgent::~VWindowAgent() = default;

QList<QWidget*> VWindowAgent::titleBars() const {
    return d_->controller.titleBars();
}

bool VWindowAgent::addTitleBar(QWidget* titleBar) {
    return d_->controller.addTitleBar(titleBar);
}

bool VWindowAgent::removeTitleBar(QWidget* titleBar) {
    return d_->controller.removeTitleBar(titleBar);
}

void VWindowAgent::clearTitleBars() {
    d_->controller.clearTitleBars();
}

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
std::optional<QPoint> VWindowAgent::trafficLightOrigin() const noexcept {
    return d_->controller.trafficLightOrigin();
}

void VWindowAgent::setTrafficLightOrigin(const QPoint& origin) {
    static_cast<void>(d_->controller.setTrafficLightOrigin(origin));
}
#endif

bool VWindowAgent::isHitTestVisible(const QWidget* widget) const {
    return d_->controller.isHitTestVisible(widget);
}

bool VWindowAgent::setHitTestVisible(QWidget* widget, const bool visible) {
    return d_->controller.setHitTestVisible(widget, visible);
}

bool VWindowAgent::isResizable() const noexcept {
    return d_->controller.isResizable();
}

void VWindowAgent::setResizable(const bool resizable) {
    if (d_->controller.setResizable(resizable)) {
        emit resizableChanged(resizable);
    }
}

VSystemButtons VWindowAgent::systemButtons() const noexcept {
    return d_->controller.systemButtons();
}

void VWindowAgent::setSystemButtons(const VSystemButtons buttons) {
    if (d_->controller.setSystemButtons(buttons)) {
        emit systemButtonsChanged(buttons);
    }
}

bool VWindowAgent::systemButtonsVisible() const noexcept {
    return d_->controller.systemButtonsVisible();
}

void VWindowAgent::setSystemButtonsVisible(const bool visible) {
    if (d_->controller.setSystemButtonsVisible(visible)) {
        emit systemButtonsVisibleChanged(visible);
    }
}

void VWindowAgent::showSystemMenu(const QPoint& position) {
    d_->controller.showSystemMenu(position);
}

void VWindowAgent::centralize() {
    d_->controller.centralize();
}

void VWindowAgent::raiseWindow() {
    d_->controller.raiseWindow();
}

} // namespace vkui
