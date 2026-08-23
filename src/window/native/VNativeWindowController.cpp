// SPDX-License-Identifier: MIT

#include "VNativeWindowController_p.h"
#include "VPlatformWindowBackend_p.h"

#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QScreen>
#include <QThread>
#include <QWidget>
#include <QWindow>
#include <algorithm>

namespace vkui::windowing {
namespace {

VSystemButtons systemButtonsFromWindowFlags(const Qt::WindowFlags flags) {
    VSystemButtons buttons;
    if (flags.testFlag(Qt::WindowCloseButtonHint)) {
        buttons |= VSystemButton::Close;
    }
    if (flags.testFlag(Qt::WindowMinimizeButtonHint)) {
        buttons |= VSystemButton::Minimize;
    }
    if (flags.testFlag(Qt::WindowMaximizeButtonHint)) {
        buttons |= VSystemButton::Maximize;
    }
    if (buttons == VSystemButtons{} && !flags.testFlag(Qt::CustomizeWindowHint)) {
        return VStandardSystemButtons;
    }
    return buttons;
}

} // namespace

VNativeWindowController::VNativeWindowController(QWidget& host)
    : backend_(createPlatformWindowBackend(*this)) {
    if (!host.isWindow() || backend_ == nullptr || host.thread() != QThread::currentThread()) {
        return;
    }

    host_ = &host;
    applicationWindowFlags_ = host_->windowFlags();
    systemButtons_ = systemButtonsFromWindowFlags(applicationWindowFlags_);
    host_->setAttribute(Qt::WA_DontCreateNativeAncestors);
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    host_->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
#endif
    // Qt 6.9 introduced a public full-content contract that preserves native
    // window semantics. Older supported Qt versions use a frameless client
    // model while the backend restores the native titled/thick frame.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    host_->setWindowFlag(Qt::ExpandedClientAreaHint, true);
    host_->setWindowFlag(Qt::NoTitleBarBackgroundHint, true);
#else
    host_->setWindowFlag(Qt::FramelessWindowHint, true);
#endif
    host_->installEventFilter(this);

    // A top-level QWidget owns its platform window. Creating the handle here
    // makes attachment deterministic and removes the need for global native hooks.
    static_cast<void>(host_->winId());
    refreshNativeHandle();
}

VNativeWindowController::~VNativeWindowController() {
    detachNativeHandle();
    if (host_ != nullptr) {
        host_->removeEventFilter(this);
    }
}

bool VNativeWindowController::isReady() const noexcept {
    return host_ != nullptr;
}

QList<QWidget*> VNativeWindowController::titleBars() const {
    QList<QWidget*> result;
    result.reserve(titleBars_.size());
    for (QWidget* titleBar : titleBars_) {
        if (titleBar != nullptr) {
            result.append(titleBar);
        }
    }
    return result;
}

bool VNativeWindowController::addTitleBar(QWidget* titleBarWidget) {
    pruneNullWidgets();
    if (!belongsToHost(titleBarWidget) || titleBars_.contains(titleBarWidget)) {
        return false;
    }
    titleBars_.append(titleBarWidget);
    return true;
}

bool VNativeWindowController::removeTitleBar(QWidget* titleBarWidget) {
    return titleBars_.removeAll(titleBarWidget) > 0;
}

void VNativeWindowController::clearTitleBars() {
    titleBars_.clear();
}

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
std::optional<QPoint> VNativeWindowController::trafficLightOrigin() const noexcept {
    return trafficLightOrigin_;
}

bool VNativeWindowController::setTrafficLightOrigin(const QPoint& origin) {
    if (trafficLightOrigin_ == origin) {
        return false;
    }
    trafficLightOrigin_ = origin;
    if (backendAttached_) {
        backend_->updateSystemButtonLayout();
    }
    return true;
}
#endif

bool VNativeWindowController::isHitTestVisible(const QWidget* widget) const {
    return widget != nullptr && hitTestVisibleWidgets_.contains(widget);
}

bool VNativeWindowController::setHitTestVisible(QWidget* widget, const bool visible) {
    pruneNullWidgets();
    if (!belongsToHost(widget)) {
        return false;
    }
    if (visible) {
        if (!hitTestVisibleWidgets_.contains(widget)) {
            hitTestVisibleWidgets_.append(widget);
        }
    } else {
        hitTestVisibleWidgets_.removeAll(widget);
    }
    return true;
}

bool VNativeWindowController::isResizable() const noexcept {
    return resizable_;
}

bool VNativeWindowController::setResizable(const bool resizable) {
    if (resizable_ == resizable) {
        return false;
    }
    resizable_ = resizable;
    if (backendAttached_) {
        backend_->updateResizablePolicy();
    }
    return true;
}

VSystemButtons VNativeWindowController::systemButtons() const noexcept {
    return systemButtons_;
}

bool VNativeWindowController::setSystemButtons(const VSystemButtons buttons) {
    if (systemButtons_ == buttons) {
        return false;
    }
    systemButtons_ = buttons;
    if (backendAttached_) {
        backend_->configureSystemButtons();
        backend_->updateResizablePolicy();
        backend_->updateSystemButtonLayout();
        backend_->applySystemButtonState();
    }
    return true;
}

bool VNativeWindowController::systemButtonsVisible() const noexcept {
    return systemButtonsVisible_;
}

bool VNativeWindowController::setSystemButtonsVisible(const bool visible) {
    if (systemButtonsVisible_ == visible) {
        return false;
    }
    systemButtonsVisible_ = visible;
    if (backendAttached_) {
        backend_->applySystemButtonState();
    }
    return true;
}

void VNativeWindowController::showSystemMenu(const QPoint& globalPosition) {
    if (backendAttached_) {
        backend_->showSystemMenu(globalPosition);
    }
}

void VNativeWindowController::centralize() {
    if (host_ == nullptr) {
        return;
    }
    if (backendAttached_ && backend_->centralize()) {
        return;
    }
    QScreen* screen = window_ != nullptr ? window_->screen() : QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return;
    }
    const QRect available = screen->availableGeometry();
    const QRect frame = host_->frameGeometry();
    host_->move(available.center() - (frame.center() - frame.topLeft()));
}

void VNativeWindowController::raiseWindow() {
    if (backendAttached_) {
        backend_->raiseWindow();
    } else if (host_ != nullptr) {
        host_->showNormal();
        host_->raise();
        host_->activateWindow();
    }
}

QWidget* VNativeWindowController::host() const noexcept {
    return host_;
}

QWindow* VNativeWindowController::window() const noexcept {
    return window_;
}

Qt::WindowFlags VNativeWindowController::windowFlags() const {
    return applicationWindowFlags_;
}

bool VNativeWindowController::hostSizeFixed() const {
    return !resizable_ || host_ == nullptr ||
           applicationWindowFlags_.testFlag(Qt::MSWindowsFixedSizeDialogHint) ||
           host_->minimumSize() == host_->maximumSize();
}

bool VNativeWindowController::pointIsDraggable(const QPoint& windowPosition) const {
    if (host_ == nullptr) {
        return false;
    }
    const QRect hostRect(QPoint{}, host_->size());
    bool insideTitleBar = false;
    for (QWidget* bar : titleBars_) {
        if (bar == nullptr || !bar->isVisible() || !bar->isEnabled()) {
            continue;
        }
        const QRect barGeometry = geometryInHost(bar);
        if (barGeometry.intersects(hostRect) && barGeometry.contains(windowPosition)) {
            insideTitleBar = true;
            break;
        }
    }
    if (!insideTitleBar) {
        return false;
    }
    for (QWidget* widget : hitTestVisibleWidgets_) {
        if (widget != nullptr && widget->isVisible() && widget->isEnabled() &&
            geometryInHost(widget).contains(windowPosition)) {
            return false;
        }
    }
    return true;
}

void VNativeWindowController::synchronizeNativeWindow() {
    if (backendAttached_) {
        backend_->synchronize();
    }
}

bool VNativeWindowController::eventFilter(QObject* watched, QEvent* event) {
    if (watched == host_) {
        switch (event->type()) {
        case QEvent::WinIdChange:
        case QEvent::Show:
            refreshNativeHandle();
            break;
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::WindowStateChange:
            synchronizeNativeWindow();
            break;
        case QEvent::Destroy:
            detachNativeHandle();
            titleBars_.clear();
            hitTestVisibleWidgets_.clear();
            host_ = nullptr;
            break;
        default:
            break;
        }
        return false;
    }

    if (watched != window_ || backend_ == nullptr) {
        return QObject::eventFilter(watched, event);
    }

    const QEvent::Type type = event->type();
    if (type < QEvent::MouseButtonPress || type > QEvent::MouseMove) {
        return false;
    }
    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    const QPoint windowPosition = mouseEvent->scenePosition().toPoint();
    const QPoint globalPosition = mouseEvent->globalPosition().toPoint();
    switch (type) {
    case QEvent::MouseButtonPress:
        if (mouseEvent->button() == Qt::LeftButton && pointIsDraggable(windowPosition)) {
            moveState_ = MoveState::Armed;
            backend_->prepareSystemMove();
            event->accept();
            return true;
        }
        if (mouseEvent->button() == Qt::RightButton && pointIsDraggable(windowPosition)) {
            backend_->showSystemMenu(globalPosition);
        }
        moveState_ = MoveState::Idle;
        break;
    case QEvent::MouseMove:
        if (moveState_ == MoveState::Armed) {
            moveState_ = MoveState::Moving;
            backend_->startSystemMove(globalPosition);
            event->accept();
            return true;
        }
        if (moveState_ == MoveState::Moving) {
            return true;
        }
        break;
    case QEvent::MouseButtonRelease:
        if (moveState_ != MoveState::Idle) {
            moveState_ = MoveState::Idle;
            backend_->cancelSystemMove();
            event->accept();
            return true;
        }
        break;
    case QEvent::MouseButtonDblClick:
        if (mouseEvent->button() == Qt::LeftButton && pointIsDraggable(windowPosition) &&
            !hostSizeFixed()) {
            moveState_ = MoveState::Idle;
            backend_->handleTitleBarDoubleClick(globalPosition);
            event->accept();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

bool VNativeWindowController::belongsToHost(const QWidget* widget) const noexcept {
    return isReady() && widget != nullptr && widget->window() == host_;
}

QRect VNativeWindowController::geometryInHost(const QWidget* widget) const {
    return widget != nullptr && host_ != nullptr
               ? QRect(widget->mapTo(host_, QPoint{}), widget->size())
               : QRect{};
}

void VNativeWindowController::refreshNativeHandle() {
    if (refreshingNativeHandle_ || host_ == nullptr || backend_ == nullptr) {
        return;
    }
    refreshingNativeHandle_ = true;

    QWindow* nextWindow = host_->windowHandle();
    const WId nextId = host_->internalWinId();
    if (nextWindow == window_ && nextId == nativeId_ && backendAttached_) {
        backend_->synchronize();
        refreshingNativeHandle_ = false;
        return;
    }

    // WinIdChange is emitted while Qt replaces or destroys the native handle.
    // Detach never sends style or frame mutations to that old handle.
    detachNativeHandle();
    window_ = nextWindow;
    nativeId_ = nextId;
    if (window_ != nullptr && nativeId_ != 0) {
        window_->installEventFilter(this);
        backendAttached_ = backend_->attach(host_, window_, nativeId_);
        if (backendAttached_) {
            applyBackendState();
        }
    }
    refreshingNativeHandle_ = false;
}

void VNativeWindowController::detachNativeHandle() noexcept {
    if (window_ != nullptr) {
        window_->removeEventFilter(this);
    }
    if (backend_ != nullptr && backendAttached_) {
        backend_->detach();
    }
    backendAttached_ = false;
    window_ = nullptr;
    nativeId_ = 0;
    moveState_ = MoveState::Idle;
}

void VNativeWindowController::applyBackendState() {
    backend_->configureSystemButtons();
    backend_->updateResizablePolicy();
    backend_->applySystemButtonState();
    backend_->updateSystemButtonLayout();
    backend_->synchronize();
}

void VNativeWindowController::pruneNullWidgets() {
    titleBars_.erase(
        std::remove_if(titleBars_.begin(), titleBars_.end(),
                       [](const QPointer<QWidget>& widget) { return widget == nullptr; }),
        titleBars_.end());
    hitTestVisibleWidgets_.erase(
        std::remove_if(hitTestVisibleWidgets_.begin(), hitTestVisibleWidgets_.end(),
                       [](const QPointer<QWidget>& widget) { return widget == nullptr; }),
        hitTestVisibleWidgets_.end());
}

} // namespace vkui::windowing
