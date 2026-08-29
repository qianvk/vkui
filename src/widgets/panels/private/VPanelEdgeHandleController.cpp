// SPDX-License-Identifier: MIT

#include "../../animation/private/VkWidgetAnimation_p.h"
#include "VPanelEdgeHandleController_p.h"

#include <QAbstractButton>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <algorithm>
#include <utility>
#include <vkui/core/VkThemeManager.h>

namespace vkui {

namespace {

constexpr int EdgeHitExtent = 20;
constexpr int TrackThickness = 6;
constexpr int TrackLength = 40;

class VPanelEdgeHandle final : public QAbstractButton {
  public:
    using ActivationHandler = VPanelEdgeHandleController::ActivationHandler;

    VPanelEdgeHandle(ActivationHandler activated, QWidget* parent)
        : QAbstractButton(parent), activated_(std::move(activated)),
          animation_(new VkWidgetAnimation(this, this)) {
        setObjectName(QStringLiteral("vPanelWindowEdgeHandle_left"));
        setProperty("edge", static_cast<int>(Qt::LeftEdge));
        // An edge affordance above a collapsed splitter owns this pointer location.
        setProperty("_vkui_blocksSplitterInteraction", true);
        setAccessibleName(QCoreApplication::translate("VPanelManager", "Panel edge handle"));
        setAttribute(Qt::WA_Hover, true);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);

        connect(VkThemeManager::instance(), &VkThemeManager::themeChanged, this,
                [this](quint64, const VkThemeChanges changes) {
                    if (changes.testFlag(VkThemeChange::Colors)) {
                        update();
                    }
                });
    }

  protected:
    void enterEvent(QEnterEvent* event) override {
        pointerPosition_ = event->position();
        animateTo(1.0);
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        if (pressedButton_ == Qt::NoButton) {
            animateTo(0.0);
        }
        QAbstractButton::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton) {
            QAbstractButton::mousePressEvent(event);
            return;
        }
        pressedButton_ = event->button();
        pressPosition_ = event->globalPosition();
        pointerPosition_ = event->position();
        clickCandidate_ = true;
        setDown(true);
        update();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        pointerPosition_ = event->position();
        if (clickCandidate_ && (event->globalPosition() - pressPosition_).manhattanLength() >=
                                   QApplication::startDragDistance()) {
            clickCandidate_ = false;
        }
        update();
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != pressedButton_) {
            QAbstractButton::mouseReleaseEvent(event);
            return;
        }

        const Qt::MouseButton button = pressedButton_;
        const QPoint globalPosition = event->globalPosition().toPoint();
        const bool activated = clickCandidate_ && rect().contains(event->position().toPoint());
        pressedButton_ = Qt::NoButton;
        clickCandidate_ = false;
        setDown(false);
        if (!underMouse()) {
            animateTo(0.0);
        }
        update();
        event->accept();
        if (activated && activated_) {
            activated_(button, globalPosition);
        }
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        event->accept();
    }

    void paintEvent(QPaintEvent*) override {
        if (!isEnabled() || opacity_ <= 0.0) {
            return;
        }

        const qreal top = std::clamp(pointerPosition_.y() - TrackLength * 0.5, 0.0,
                                     std::max(0.0, height() - qreal(TrackLength)));
        const QRectF track(0.0, top, TrackThickness, TrackLength);
        QColor color = isDown() ? VkThemeManager::instance()->theme().colors().accentPressed
                                : VkThemeManager::instance()->theme().colors().accent;
        color.setAlphaF(static_cast<float>(color.alphaF() * opacity_));

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(track, TrackThickness * 0.5, TrackThickness * 0.5);
    }

    void changeEvent(QEvent* event) override {
        QAbstractButton::changeEvent(event);
        if (event->type() == QEvent::LanguageChange) {
            setAccessibleName(QCoreApplication::translate("VPanelManager", "Panel edge handle"));
        }
    }

  private:
    void animateTo(const qreal target) {
        animation_->start(
            opacity_, target, target > opacity_ ? VkMotionRole::Enter : VkMotionRole::Exit,
            [this](const qreal value) {
                opacity_ = value;
                update();
            },
            {}, 0.55);
    }

    ActivationHandler activated_;
    VkWidgetAnimation* animation_ = nullptr;
    QPointF pointerPosition_;
    QPointF pressPosition_;
    Qt::MouseButton pressedButton_ = Qt::NoButton;
    qreal opacity_ = 0.0;
    bool clickCandidate_ = false;
};

} // namespace

VPanelEdgeHandleController::VPanelEdgeHandleController(QWidget& window, ActivationHandler activated,
                                                       QObject* parent)
    : QObject(parent), window_(&window) {
    window_->installEventFilter(this);
    handle_ = new VPanelEdgeHandle(std::move(activated), window_);
    handle_->show();
    updateHandleGeometry();
}

VPanelEdgeHandleController::~VPanelEdgeHandleController() {
    if (window_ != nullptr) {
        window_->removeEventFilter(this);
    }
    delete handle_.data();
}

QList<QWidget*> VPanelEdgeHandleController::handles() const {
    return handle_ != nullptr ? QList<QWidget*>{handle_.data()} : QList<QWidget*>{};
}

bool VPanelEdgeHandleController::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window_ && event != nullptr &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        updateHandleGeometry();
    }
    return QObject::eventFilter(watched, event);
}

void VPanelEdgeHandleController::updateHandleGeometry() {
    if (window_ == nullptr || handle_ == nullptr) {
        return;
    }

    const QRect bounds = window_->rect();
    handle_->setGeometry(bounds.left(), bounds.top(), EdgeHitExtent, bounds.height());
    handle_->raise();
}

} // namespace vkui
