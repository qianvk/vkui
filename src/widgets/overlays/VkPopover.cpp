// SPDX-License-Identifier: MIT

#include "private/VkPopoverPath_p.h"
#include "private/VkPopover_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QTouchEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QAbstractButton>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/overlays/VkPopover.h>

namespace vkui {
namespace {

bool validPlacement(VkPopoverPlacement placement) noexcept {
    switch (placement) {
    case VkPopoverPlacement::Automatic:
    case VkPopoverPlacement::Below:
    case VkPopoverPlacement::Above:
    case VkPopoverPlacement::Right:
    case VkPopoverPlacement::Left:
        return true;
    }
    return false;
}

VkPopoverGeometryMetrics popoverGeometryMetrics(const VkMetricTokens& metrics) {
    return {
        metrics.spacing12,           metrics.popoverCornerRadius,
        metrics.popoverArrowWidth,   metrics.popoverArrowDepth,
        metrics.popoverScreenMargin, metrics.popoverAnchorGap,
        metrics.popoverShadowRadius, metrics.spacing2,
    };
}

} // namespace

VkPopoverPrivate::VkPopoverPrivate(VkPopover* popover) : q(popover), animation(popover) {
    auto* manager = VkThemeManager::instance();
    geometryMetrics = popoverGeometryMetrics(manager->theme().metrics());
    themeChangedConnection = connect(manager, &VkThemeManager::themeChanged, q, [this](quint64) {
        const VkPopoverGeometryMetrics nextMetrics =
            popoverGeometryMetrics(VkThemeManager::instance()->theme().metrics());
        if (geometryMetrics != nextMetrics) {
            geometryMetrics = nextMetrics;
            queueReposition();
        }
        // Color-only changes repaint immediately. The shadow cache compares
        // its actual color input and remains hot for accent-only changes.
        q->update();
    });
}

VkPopoverPrivate::~VkPopoverPrivate() {
    shutdown();
}

void VkPopoverPrivate::setContentWidget(QWidget* newContent) {
    if (content == newContent || newContent == q || (newContent && newContent->isAncestorOf(q))) {
        return;
    }

    if (contentDestroyedConnection) {
        disconnect(contentDestroyedConnection);
        contentDestroyedConnection = {};
    }
    QWidget* oldContent = content.data();
    if (oldContent && filtersAttached) {
        oldContent->removeEventFilter(this);
    }
    content = nullptr;
    delete oldContent;

    if (!newContent) {
        if (state != State::Closed) {
            closeAnimated();
        }
        return;
    }

    newContent->setParent(q);
    content = newContent;
    contentDestroyedConnection = connect(newContent, &QObject::destroyed, q, [this] {
        content = nullptr;
        contentDestroyedConnection = {};
        if (state != State::Closed) {
            closeAnimated();
        }
    });
    if (filtersAttached) {
        newContent->installEventFilter(this);
    }
    if (q->isVisible()) {
        newContent->show();
    }
    queueReposition();
}

QWidget* VkPopoverPrivate::contentWidget() const noexcept {
    return content.data();
}

void VkPopoverPrivate::setPreferredPlacement(VkPopoverPlacement placement) {
    if (!validPlacement(placement) || preferredPlacement == placement) {
        return;
    }
    preferredPlacement = placement;
    queueReposition();
}

void VkPopoverPrivate::setClosePolicy(VkPopoverClosePolicy policy) noexcept {
    closePolicy = policy;
}

void VkPopoverPrivate::openFor(QWidget* newAnchor, const QRect& rectInAnchor) {
    if (!newAnchor || !content || newAnchor == q || q->isAncestorOf(newAnchor) ||
        newAnchor->thread() != q->thread()) {
        return;
    }
    Q_ASSERT_X(QThread::currentThread() == q->thread(), "VkPopover::openFor",
               "VkPopover must be used from its GUI thread");

    const State previousState = state;
    if (previousState == State::Closing) {
        animation.stop();
    }

    if (filtersAttached) {
        detachOpenFilters();
    }
    anchor = newAnchor;
    anchorLocalRect = rectInAnchor;
    attachOpenFilters();

    if (!repositionNow()) {
        detachOpenFilters();
        if (previousState == State::Open || previousState == State::Opening ||
            previousState == State::Closing) {
            state = previousState;
            closeImmediately();
        }
        return;
    }

    if (previousState == State::Open || previousState == State::Opening) {
        if (!q->isVisible()) {
            q->show();
        }
        q->raise();
        return;
    }

    Q_EMIT q->aboutToOpen();
    state = State::Opening;
    const bool freshOpen = previousState == State::Closed;
    if (freshOpen) {
        currentOpacity = 0.0;
    }
    applyOpacityFrame(currentOpacity);

    q->show();
    if (content) {
        content->show();
    }
    q->raise();
    q->setFocus(Qt::PopupFocusReason);
    startOpenAnimation();
}

void VkPopoverPrivate::closeAnimated() {
    if (state == State::Closed || state == State::Closing) {
        return;
    }

    Q_EMIT q->aboutToClose();
    state = State::Closing;
    const qreal startOpacity = currentOpacity;
    animation.start(0.0, 1.0, VkMotionRole::Exit,
                    [this, startOpacity](qreal progress) {
                        applyOpacityFrame(startOpacity * (1.0 - progress));
                    },
                    [this] { finishClosing(); });
}

void VkPopoverPrivate::closeImmediately() {
    if (state == State::Closed) {
        return;
    }
    if (state != State::Closing) {
        Q_EMIT q->aboutToClose();
        state = State::Closing;
    }
    animation.stop();
    applyOpacityFrame(0.0);
    finishClosing();
}

void VkPopoverPrivate::shutdown() {
    animation.stop();
    detachOpenFilters();
    if (themeChangedConnection) {
        disconnect(themeChangedConnection);
        themeChangedConnection = {};
    }
    if (contentDestroyedConnection) {
        disconnect(contentDestroyedConnection);
        contentDestroyedConnection = {};
    }
    content = nullptr;
    state = State::Closed;
    q = nullptr;
}

bool VkPopoverPrivate::isOpen() const noexcept {
    return state == State::Opening || state == State::Open;
}

void VkPopoverPrivate::attachOpenFilters() {
    if (filtersAttached || !anchor) {
        return;
    }
    filtersAttached = true;
    anchor->installEventFilter(this);
    anchorWindow = anchor->window();
    if (anchorWindow && anchorWindow != anchor) {
        anchorWindow->installEventFilter(this);
    }
    if (content) {
        content->installEventFilter(this);
    }
    if (qApp) {
        qApp->installEventFilter(this);
    }
    anchorDestroyedConnection =
        connect(anchor.data(), &QObject::destroyed, q, [this] { handleAnchorDestroyed(); });
    reconnectWindowAndScreen();
}

void VkPopoverPrivate::detachOpenFilters() {
    if (!filtersAttached) {
        anchor = nullptr;
        anchorWindow = nullptr;
        setObservedScreen(nullptr);
        return;
    }
    if (anchor) {
        anchor->removeEventFilter(this);
    }
    if (anchorWindow && anchorWindow != anchor) {
        anchorWindow->removeEventFilter(this);
    }
    if (content) {
        content->removeEventFilter(this);
    }
    if (qApp) {
        qApp->removeEventFilter(this);
    }
    filtersAttached = false;
    removeAnchorFilters();
    setObservedScreen(nullptr);
    repositionQueued = false;
}

void VkPopoverPrivate::removeAnchorFilters() {
    if (anchorDestroyedConnection) {
        disconnect(anchorDestroyedConnection);
        anchorDestroyedConnection = {};
    }
    if (windowScreenConnection) {
        disconnect(windowScreenConnection);
        windowScreenConnection = {};
    }
    anchor = nullptr;
    anchorWindow = nullptr;
}

void VkPopoverPrivate::reconnectWindowAndScreen() {
    if (!anchor) {
        return;
    }

    QWidget* newWindow = anchor->window();
    if (newWindow != anchorWindow) {
        if (filtersAttached && anchorWindow && anchorWindow != anchor) {
            anchorWindow->removeEventFilter(this);
        }
        anchorWindow = newWindow;
        if (filtersAttached && anchorWindow && anchorWindow != anchor) {
            anchorWindow->installEventFilter(this);
        }
    }
    if (windowScreenConnection) {
        disconnect(windowScreenConnection);
        windowScreenConnection = {};
    }
    if (anchorWindow && anchorWindow->windowHandle()) {
        windowScreenConnection =
            connect(anchorWindow->windowHandle(), &QWindow::screenChanged, q, [this](QScreen*) {
                reconnectWindowAndScreen();
                queueReposition();
            });
    }
    const QRectF globalAnchor = anchorGlobalRect();
    setObservedScreen(screenForAnchor(globalAnchor));
}

void VkPopoverPrivate::setObservedScreen(QScreen* screen) {
    if (observedScreen == screen) {
        return;
    }
    if (screenAvailableConnection) {
        disconnect(screenAvailableConnection);
        screenAvailableConnection = {};
    }
    if (screenGeometryConnection) {
        disconnect(screenGeometryConnection);
        screenGeometryConnection = {};
    }
    observedScreen = screen;
    if (screen) {
        screenAvailableConnection = connect(screen, &QScreen::availableGeometryChanged, q,
                                            [this](const QRect&) { queueReposition(); });
        screenGeometryConnection = connect(screen, &QScreen::geometryChanged, q,
                                           [this](const QRect&) { queueReposition(); });
    }
}

void VkPopoverPrivate::queueReposition() {
    if (repositionQueued || state == State::Closed || !anchor) {
        return;
    }
    repositionQueued = true;
    QMetaObject::invokeMethod(
        q,
        [this] {
            repositionQueued = false;
            if (state != State::Closed && anchor && !repositionNow()) {
                closeImmediately();
            }
        },
        Qt::QueuedConnection);
}

QRectF VkPopoverPrivate::anchorGlobalRect() const {
    if (!anchor) {
        return {};
    }
    const QRect local = anchorLocalRect.isEmpty() ? anchor->rect() : anchorLocalRect.normalized();
    const QPoint globalTopLeft = anchor->mapToGlobal(local.topLeft());
    return QRectF(QPointF(globalTopLeft), QSizeF(local.size()));
}

QSizeF VkPopoverPrivate::desiredContentSize() const {
    if (!content) {
        return {};
    }
    QSize desired = content->sizeHint();
    if (!desired.isValid() || desired.isEmpty()) {
        desired = content->size();
    }
    const QSize minimumHint = content->minimumSizeHint();
    if (minimumHint.isValid()) {
        desired = desired.expandedTo(minimumHint);
    }
    desired = desired.expandedTo(content->minimumSize());
    const QSize maximum = content->maximumSize();
    desired.setWidth(std::min(desired.width(), maximum.width()));
    desired.setHeight(std::min(desired.height(), maximum.height()));
    desired.setWidth(std::max(1, desired.width()));
    desired.setHeight(std::max(1, desired.height()));
    return QSizeF(desired);
}

QScreen* VkPopoverPrivate::screenForAnchor(const QRectF& globalAnchor) const {
    if (QScreen* screen = QGuiApplication::screenAt(globalAnchor.center().toPoint())) {
        return screen;
    }
    if (anchorWindow && anchorWindow->windowHandle() && anchorWindow->windowHandle()->screen()) {
        return anchorWindow->windowHandle()->screen();
    }
    return QGuiApplication::primaryScreen();
}

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
QAbstractButton* buttonAtGlobalPoint(const QPoint& globalPoint, QWidget* expectedWindow,
                                     QWidget* popover) {
    QWidget* widget = QApplication::widgetAt(globalPoint);
    while (widget) {
        if (widget == popover || (popover && popover->isAncestorOf(widget))) {
            return nullptr;
        }
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
            return button->isEnabled() && button->isVisible() &&
                           (!expectedWindow || button->window() == expectedWindow)
                       ? button
                       : nullptr;
        }
        if (widget->isWindow()) {
            return nullptr;
        }
        widget = widget->parentWidget();
    }
    return nullptr;
}
#endif

bool VkPopoverPrivate::repositionNow() {
    if (!anchor || !content) {
        return false;
    }
    reconnectWindowAndScreen();
    if (!observedScreen) {
        return false;
    }

    const VkTheme& theme = VkThemeManager::instance()->theme();
    const VkMetricTokens& metrics = theme.metrics();
    const qreal shadowOffset = metrics.spacing2;

    VkPopoverPlacementInput input;
    input.anchorRect = anchorGlobalRect();
    input.contentSize = desiredContentSize();
    input.preferredPlacement = preferredPlacement;
    input.availableGeometry = QRectF(observedScreen->availableGeometry());
    input.screenMargin = metrics.popoverScreenMargin;
    input.anchorGap = metrics.popoverAnchorGap;
    input.bodyCornerRadius = metrics.popoverCornerRadius;
    input.arrowWidth = metrics.popoverArrowWidth;
    input.arrowDepth = metrics.popoverArrowDepth;
    input.layoutDirection = anchor->layoutDirection();
    input.contentMargins =
        QMarginsF(metrics.spacing12, metrics.spacing12, metrics.spacing12, metrics.spacing12);
    input.outerMargin = std::ceil(metrics.popoverShadowRadius + shadowOffset);

    const VkPopoverPlacementResult result = VkPopoverPlacementEngine::calculate(input);
    if (!result.isValid()) {
        return false;
    }
    finalPlacement = result;
    finalPath = VkPopoverPath::create(result.bodyRect, result.resolvedPlacement, result.arrowTip,
                                      result.arrowBaseCenter, metrics.popoverArrowWidth,
                                      metrics.popoverCornerRadius);
    if (finalPath.isEmpty()) {
        return false;
    }
    const QRect popupRect = finalPlacement.popupRect;
    if (q->geometry() != popupRect) {
        q->setGeometry(popupRect);
    }
    const QRect contentRect = finalPlacement.contentRect.toAlignedRect().intersected(q->rect());
    if (content && content->geometry() != contentRect) {
        content->setGeometry(contentRect);
    }
    return true;
}

void VkPopoverPrivate::applyOpacityFrame(qreal opacity) {
    if (!q) {
        return;
    }
    currentOpacity = std::clamp(opacity, 0.0, 1.0);
    if (!qFuzzyCompare(q->windowOpacity() + 1.0, currentOpacity + 1.0)) {
        q->setWindowOpacity(currentOpacity);
    }
}

void VkPopoverPrivate::startOpenAnimation() {
    animation.start(currentOpacity, 1.0, VkMotionRole::EmphasizedEnter,
                    [this](qreal opacity) { applyOpacityFrame(opacity); },
                    [this] { finishOpening(); });
}

void VkPopoverPrivate::finishOpening() {
    if (state != State::Opening) {
        return;
    }
    applyOpacityFrame(1.0);
    state = State::Open;
    Q_EMIT q->opened();
}

void VkPopoverPrivate::finishClosing() {
    if (state != State::Closing) {
        return;
    }
    animation.stop();
    internalHide = true;
    q->hide();
    internalHide = false;
    detachOpenFilters();
    state = State::Closed;
    currentOpacity = 1.0;
    q->setWindowOpacity(1.0);
    if (finalPlacement.isValid()) {
        q->setGeometry(finalPlacement.popupRect);
    }
    Q_EMIT q->closed();
}

void VkPopoverPrivate::handleAnchorDestroyed() {
    anchor = nullptr;
    if (closePolicy.testFlag(VkPopoverClosePolicyFlag::AnchorDestroyed)) {
        closeAnimated();
        return;
    }

    // Without the dismissal flag the last valid geometry remains usable. Stop
    // observing the dead anchor while retaining application dismissal filters.
    if (anchorWindow) {
        anchorWindow->removeEventFilter(this);
    }
    if (anchorDestroyedConnection) {
        disconnect(anchorDestroyedConnection);
        anchorDestroyedConnection = {};
    }
    if (windowScreenConnection) {
        disconnect(windowScreenConnection);
        windowScreenConnection = {};
    }
    anchorWindow = nullptr;
}

bool VkPopoverPrivate::eventFilter(QObject* watched, QEvent* event) {
    if (!event || state == State::Closed) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress &&
        closePolicy.testFlag(VkPopoverClosePolicyFlag::EscapeKey)) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            keyEvent->accept();
            closeAnimated();
            return true;
        }
    }

    if (closePolicy.testFlag(VkPopoverClosePolicyFlag::OutsideClick)) {
        QPointF globalPosition;
        bool pointerPress = false;
        if (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::NonClientAreaMouseButtonPress) {
            globalPosition = static_cast<QMouseEvent*>(event)->globalPosition();
            pointerPress = true;
        } else if (event->type() == QEvent::TouchBegin) {
            const auto* touchEvent = static_cast<QTouchEvent*>(event);
            if (!touchEvent->points().isEmpty()) {
                globalPosition = touchEvent->points().constFirst().globalPosition();
                pointerPress = true;
            }
        }
        const QPoint globalPoint = globalPosition.toPoint();
        const bool insidePopover = q->geometry().contains(globalPoint);
        const QRect anchorRect =
            anchor ? anchorGlobalRect().toAlignedRect().adjusted(-1, -1, 1, 1) : QRect();
        const bool insideAnchor = anchorRect.contains(globalPoint);
        if (pointerPress && !insidePopover && !insideAnchor) {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
            QPointer<QAbstractButton> forwardedButton =
                buttonAtGlobalPoint(globalPoint, anchorWindow, q);
            closeAnimated();
            if (forwardedButton) {
                QTimer::singleShot(0, q, [forwardedButton] {
                    if (forwardedButton && forwardedButton->isEnabled() &&
                        forwardedButton->isVisible()) {
                        forwardedButton->click();
                    }
                });
                return true;
            }
#else
            closeAnimated();
#endif
        }
    }

    if (event->type() == QEvent::ApplicationDeactivate &&
        closePolicy.testFlag(VkPopoverClosePolicyFlag::WindowDeactivated)) {
        closeAnimated();
    }
    if (event->type() == QEvent::LayoutDirectionChange) {
        queueReposition();
    }

    if (watched == anchor) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::ParentChange:
        case QEvent::LayoutDirectionChange:
        case QEvent::Show:
            reconnectWindowAndScreen();
            queueReposition();
            break;
        case QEvent::Hide:
        case QEvent::Close:
            closeAnimated();
            break;
        default:
            break;
        }
    } else if (watched == anchorWindow) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::WindowStateChange:
        case QEvent::LayoutDirectionChange:
        case QEvent::ScreenChangeInternal:
            reconnectWindowAndScreen();
            queueReposition();
            break;
        case QEvent::Hide:
        case QEvent::Close:
            closeAnimated();
            break;
        default:
            break;
        }
    } else if (watched == content) {
        switch (event->type()) {
        case QEvent::LayoutRequest:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::PolishRequest:
            queueReposition();
            break;
        default:
            break;
        }
    } else if (anchor) {
        // Moving an intermediate parent changes the anchor's global position
        // without necessarily delivering a move event to the anchor itself.
        // The application filter lets scrolling containers and nested panels
        // participate without installing long-lived filters on the hierarchy.
        auto* ancestor = qobject_cast<QWidget*>(watched);
        if (ancestor && ancestor->isAncestorOf(anchor)) {
            switch (event->type()) {
            case QEvent::Move:
            case QEvent::Resize:
            case QEvent::ParentChange:
            case QEvent::LayoutRequest:
            case QEvent::LayoutDirectionChange:
                reconnectWindowAndScreen();
                queueReposition();
                break;
            case QEvent::Hide:
            case QEvent::Close:
                closeAnimated();
                break;
            default:
                break;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

bool VkPopoverPrivate::handleEvent(QEvent* event) {
    if (!event) {
        return false;
    }
    switch (event->type()) {
    case QEvent::Close:
        if (state != State::Closed) {
            // The private policy and animation path own dismissal, so keep the
            // native window alive until that path has completed.
            static_cast<QCloseEvent*>(event)->ignore();
            return true;
        }
        break;
    case QEvent::WindowDeactivate:
        if (closePolicy.testFlag(VkPopoverClosePolicyFlag::WindowDeactivated)) {
            closeAnimated();
        }
        break;
    case QEvent::Hide:
        if (!internalHide && state != State::Closed) {
            closeImmediately();
        }
        break;
    case QEvent::LayoutDirectionChange:
    case QEvent::ScreenChangeInternal:
        queueReposition();
        break;
    case QEvent::DevicePixelRatioChange:
        shadowCache.invalidate();
        q->update();
        break;
    default:
        break;
    }
    return false;
}

bool VkPopoverPrivate::handleKeyPress(QKeyEvent* event) {
    if (event && event->key() == Qt::Key_Escape &&
        closePolicy.testFlag(VkPopoverClosePolicyFlag::EscapeKey)) {
        event->accept();
        closeAnimated();
        return true;
    }
    return false;
}

void VkPopoverPrivate::paint(QPaintEvent* event) {
    Q_UNUSED(event)
    if (finalPath.isEmpty()) {
        return;
    }

    const VkTheme& theme = VkThemeManager::instance()->theme();
    const VkColorTokens& colors = theme.colors();
    const VkMetricTokens& metrics = theme.metrics();
    QPainter painter(q);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPixmap& shadow = shadowCache.shadow(
        finalPath, finalPlacement.popupRect.size(), q->devicePixelRatioF(), colors.shadow,
        metrics.popoverShadowRadius, QPointF(0.0, metrics.spacing2));
    if (!shadow.isNull()) {
        // The point overload lets QPainter apply the pixmap DPR exactly once.
        painter.drawPixmap(QPointF(0.0, 0.0), shadow);
    }

    painter.fillPath(finalPath, colors.popoverBackground);
    QPen borderPen(colors.border);
    borderPen.setWidthF(std::max<qreal>(0.5, metrics.borderWidth));
    borderPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(finalPath);
}

VkPopover::VkPopover(QWidget* parent)
    : QWidget(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      d(std::make_unique<VkPopoverPrivate>(this)) {
    Q_ASSERT_X(QThread::currentThread() == thread(), "VkPopover::VkPopover",
               "VkPopover must be created on the GUI thread");
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setWindowModality(Qt::NonModal);
    hide();
}

VkPopover::~VkPopover() = default;

void VkPopover::setContentWidget(QWidget* content) {
    d->setContentWidget(content);
}

QWidget* VkPopover::contentWidget() const noexcept {
    return d->contentWidget();
}

void VkPopover::setPreferredPlacement(VkPopoverPlacement placement) {
    d->setPreferredPlacement(placement);
}

VkPopoverPlacement VkPopover::preferredPlacement() const noexcept {
    return d->preferredPlacement;
}

void VkPopover::setClosePolicy(VkPopoverClosePolicy policy) {
    d->setClosePolicy(policy);
}

VkPopoverClosePolicy VkPopover::closePolicy() const noexcept {
    return d->closePolicy;
}

bool VkPopover::isOpen() const noexcept {
    return d->isOpen();
}

void VkPopover::openFor(QWidget* anchor) {
    openFor(anchor, {});
}

void VkPopover::openFor(QWidget* anchor, const QRect& anchorRectInAnchor) {
    d->openFor(anchor, anchorRectInAnchor);
}

void VkPopover::closeAnimated() {
    d->closeAnimated();
}

void VkPopover::closeImmediately() {
    d->closeImmediately();
}

void VkPopover::paintEvent(QPaintEvent* event) {
    d->paint(event);
}

void VkPopover::keyPressEvent(QKeyEvent* event) {
    if (!d->handleKeyPress(event)) {
        QWidget::keyPressEvent(event);
    }
}

bool VkPopover::event(QEvent* event) {
    if (d && d->handleEvent(event)) {
        return true;
    }
    return QWidget::event(event);
}

} // namespace vkui
