// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QEnterEvent>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QShowEvent>
#include <QSplitterHandle>
#include <QWindow>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/controls/VSplitter.h>

namespace vkui {

namespace {

constexpr int SeparatorExtent = 1;
constexpr int HandleExtent = 5;
constexpr auto BlocksSplitterInteractionProperty = "_vkui_blocksSplitterInteraction";

} // namespace

class VSplitterHandle final : public QSplitterHandle {
  public:
    VSplitterHandle(const Qt::Orientation orientation, VSplitter* parent)
        : QSplitterHandle(orientation, parent), owner_(parent) {
        setAttribute(Qt::WA_Hover, true);
        setAutoFillBackground(false);

        connect(VkThemeManager::instance(), &VkThemeManager::themeChanged, this,
                [this](quint64, const VkThemeChanges changes) {
                    if (changes.testFlag(VkThemeChange::Colors)) {
                        update();
                    }
                });
    }

    ~VSplitterHandle() override {
        detachCursorTracking(false);
    }

    void detachOwner() {
        detachCursorTracking(true);
        owner_ = nullptr;
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        paintPanelBackgrounds(painter);

        QRect separator = rect();
        if (orientation() == Qt::Horizontal) {
            separator.setLeft((width() - SeparatorExtent) / 2);
            separator.setWidth(SeparatorExtent);
        } else {
            separator.setTop((height() - SeparatorExtent) / 2);
            separator.setHeight(SeparatorExtent);
        }

        const bool interactionBlocked =
            (hovered_ || pressedButton_ != Qt::NoButton) && interactionBlockedAt(QCursor::pos());
        if (isEnabled() && !interactionBlocked && (hovered_ || pressedButton_ != Qt::NoButton)) {
            const auto& colors = VkThemeManager::instance()->theme().colors();
            painter.fillRect(separator,
                             pressedButton_ != Qt::NoButton ? colors.accentPressed : colors.accent);
        }
    }

    void enterEvent(QEnterEvent* event) override {
        updateCursorForPosition(QPointF(QCursor::pos()));
        QSplitterHandle::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        updateCursorForPosition(QPointF(QCursor::pos()));
        if (!cursorOwned_) {
            setHovered(false);
        }
        QSplitterHandle::leaveEvent(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != trackedWindow_) {
            return QSplitterHandle::eventFilter(watched, event);
        }

        if (event->type() == QEvent::MouseMove) {
            // Mouse events can queue while the pointer moves rapidly across a narrow handle.
            // Cursor ownership must follow the current pointer, not an older event coordinate.
            updateCursorForPosition(QPointF(QCursor::pos()));
        } else if (event->type() == QEvent::CursorChange && cursorOwned_) {
            applySplitterCursor();
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::WindowDeactivate) {
            // Native leave and activation notifications may be delivered after a newer pointer
            // position. Geometry is authoritative; never tear down the guard for a stale event.
            updateCursorForPosition(QPointF(QCursor::pos()));
        } else if (event->type() == QEvent::Hide) {
            releaseCursor(QPointF(QCursor::pos()), true);
        } else if (event->type() == QEvent::Destroy) {
            trackedWindow_.clear();
            cursorOwned_ = false;
            releaseInheritedCursorGuard();
            setHovered(false);
        }

        return QSplitterHandle::eventFilter(watched, event);
    }

    void showEvent(QShowEvent* event) override {
        QSplitterHandle::showEvent(event);
        QMetaObject::invokeMethod(this, [this] { attachCursorTracking(); }, Qt::QueuedConnection);
    }

    void hideEvent(QHideEvent* event) override {
        detachCursorTracking(true);
        QSplitterHandle::hideEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
            clickCandidate_ = true;
            pressedButton_ = event->button();
            pressPosition_ = event->globalPosition();
            update();
        }
        if (event->button() == Qt::LeftButton) {
            QSplitterHandle::mousePressEvent(event);
        } else if (event->button() == Qt::RightButton) {
            event->accept();
        } else {
            QSplitterHandle::mousePressEvent(event);
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (clickCandidate_ && pressedButton_ != Qt::NoButton &&
            (event->globalPosition() - pressPosition_).manhattanLength() >=
                QApplication::startDragDistance()) {
            clickCandidate_ = false;
        }
        if (pressedButton_ == Qt::LeftButton) {
            QSplitterHandle::mouseMoveEvent(event);
        } else if (pressedButton_ == Qt::RightButton) {
            event->accept();
        } else {
            QSplitterHandle::mouseMoveEvent(event);
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        const Qt::MouseButton button = pressedButton_;
        const bool clicked = event->button() == button && clickCandidate_ &&
                             (event->globalPosition() - pressPosition_).manhattanLength() <
                                 QApplication::startDragDistance() &&
                             rect().contains(event->position().toPoint());
        if (pressedButton_ == Qt::LeftButton) {
            QSplitterHandle::mouseReleaseEvent(event);
        } else if (pressedButton_ == Qt::RightButton) {
            event->accept();
        } else {
            QSplitterHandle::mouseReleaseEvent(event);
        }
        if (event->button() == button) {
            cancelClickTracking();
            update();
            updateCursorForPosition(QPointF(QCursor::pos()));
            if (clicked) {
                owner_->queueHandleClicked(this, button);
            }
        }
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        event->accept();
    }

  private:
    [[nodiscard]] bool containsGlobalPosition(const QPointF& globalPosition) const {
        const QPoint topLeft = mapToGlobal(QPoint(0, 0));
        return globalPosition.x() >= topLeft.x() && globalPosition.x() < topLeft.x() + width() &&
               globalPosition.y() >= topLeft.y() && globalPosition.y() < topLeft.y() + height();
    }

    [[nodiscard]] QCursor cursorAt(const QPoint& globalPosition) const {
        if (const QCursor* overrideCursor = QApplication::overrideCursor()) {
            return *overrideCursor;
        }

        QWidget* target = QApplication::widgetAt(globalPosition);
        while (target != nullptr && !target->testAttribute(Qt::WA_SetCursor)) {
            target = target->parentWidget();
        }
        return target != nullptr ? target->cursor() : QCursor(Qt::ArrowCursor);
    }

    [[nodiscard]] bool interactionBlockedAt(const QPoint& globalPosition) const {
        QWidget* target = QApplication::widgetAt(globalPosition);
        while (target != nullptr && target != window()) {
            if (target != this && target->property(BlocksSplitterInteractionProperty).toBool()) {
                return true;
            }
            target = target->parentWidget();
        }
        return false;
    }

    void attachCursorTracking() {
        if (!isVisible()) {
            detachCursorTracking(true);
            return;
        }

        QWindow* topLevelWindow = window()->windowHandle();
        if (trackedWindow_ == topLevelWindow) {
            return;
        }

        detachCursorTracking(true);
        trackedWindow_ = topLevelWindow;
        if (trackedWindow_ != nullptr) {
            trackedWindow_->installEventFilter(this);
            updateCursorForPosition(QPointF(QCursor::pos()));
        }
    }

    void detachCursorTracking(const bool restoreCursor) {
        QPointer<QWindow> window = trackedWindow_;
        trackedWindow_.clear();
        if (window != nullptr) {
            window->removeEventFilter(this);
        }
        const bool restoreWindowCursor = cursorOwned_ && restoreCursor && window != nullptr;
        cursorOwned_ = false;
        releaseInheritedCursorGuard();
        if (restoreWindowCursor) {
            window->setCursor(cursorAt(QCursor::pos()));
        }
        setHovered(false);
    }

    void updateCursorForPosition(const QPointF& globalPosition) {
        const bool inside = containsGlobalPosition(globalPosition);
        const bool blocked = inside && interactionBlockedAt(globalPosition.toPoint());
        const bool shouldOwnCursor = isVisible() && isEnabled() &&
                                     (pressedButton_ == Qt::LeftButton || (inside && !blocked));
        if (shouldOwnCursor) {
            cursorOwned_ = true;
            acquireInheritedCursorGuard();
            setHovered(inside && !blocked);
            applySplitterCursor();
        } else if (cursorOwned_) {
            releaseCursor(globalPosition, true);
        }
    }

    void applySplitterCursor() {
        if (updatingWindowCursor_ || trackedWindow_ == nullptr ||
            QApplication::overrideCursor() != nullptr) {
            return;
        }

        const Qt::CursorShape shape = splitterCursor().shape();
        if (trackedWindow_->cursor().shape() == shape) {
            return;
        }

        updatingWindowCursor_ = true;
        trackedWindow_->setCursor(QCursor(shape));
        updatingWindowCursor_ = false;
    }

    void releaseCursor(const QPointF& globalPosition, const bool restoreCursor) {
        if (!cursorOwned_) {
            return;
        }

        cursorOwned_ = false;
        releaseInheritedCursorGuard();
        if (restoreCursor && trackedWindow_ != nullptr) {
            trackedWindow_->setCursor(cursorAt(globalPosition.toPoint()));
        }
        setHovered(false);
    }

    [[nodiscard]] QCursor splitterCursor() const {
        return QCursor(orientation() == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
    }

    void acquireInheritedCursorGuard() {
        if (inheritedCursorGuardActive_ || owner_ == nullptr) {
            return;
        }

        inheritedCursorGuardActive_ = true;
        owner_->acquireCursorGuard(this);
    }

    void releaseInheritedCursorGuard() {
        if (!inheritedCursorGuardActive_ || owner_ == nullptr) {
            return;
        }

        inheritedCursorGuardActive_ = false;
        owner_->releaseCursorGuard(this);
    }

    void setHovered(const bool hovered) {
        if (hovered_ == hovered) {
            return;
        }
        hovered_ = hovered;
        update();
    }

    void paintPanelBackgrounds(QPainter& painter) const {
        int handleIndex = -1;
        for (int index = 1; index < owner_->count(); ++index) {
            if (owner_->handle(index) == this) {
                handleIndex = index;
                break;
            }
        }
        if (handleIndex < 1) {
            return;
        }

        QWidget* first = owner_->widget(handleIndex - 1);
        QWidget* second = owner_->widget(handleIndex);
        if (first == nullptr || second == nullptr) {
            return;
        }

        const auto panelColor = [](const QWidget* panel) {
            return panel->palette().color(panel->backgroundRole());
        };
        const bool firstPrecedesSecond =
            orientation() == Qt::Horizontal
                ? first->geometry().center().x() <= second->geometry().center().x()
                : first->geometry().center().y() <= second->geometry().center().y();
        const QColor precedingColor = panelColor(firstPrecedesSecond ? first : second);
        const QColor followingColor = panelColor(firstPrecedesSecond ? second : first);

        if (orientation() == Qt::Horizontal) {
            const int boundary = (width() + 1) / 2;
            painter.fillRect(QRect(0, 0, boundary, height()), precedingColor);
            painter.fillRect(QRect(boundary, 0, width() - boundary, height()), followingColor);
        } else {
            const int boundary = (height() + 1) / 2;
            painter.fillRect(QRect(0, 0, width(), boundary), precedingColor);
            painter.fillRect(QRect(0, boundary, width(), height() - boundary), followingColor);
        }
    }

    void cancelClickTracking() {
        pressedButton_ = Qt::NoButton;
        clickCandidate_ = false;
    }

    VSplitter* owner_ = nullptr;
    QPointF pressPosition_;
    QPointer<QWindow> trackedWindow_;
    Qt::MouseButton pressedButton_ = Qt::NoButton;
    bool hovered_{false};
    bool clickCandidate_{false};
    bool cursorOwned_{false};
    bool inheritedCursorGuardActive_{false};
    bool updatingWindowCursor_{false};
};

VSplitter::VSplitter(QWidget* parent) : VSplitter(Qt::Horizontal, parent) {}

VSplitter::VSplitter(const Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent) {
    // A concrete five-pixel handle has the same grab extent as Qt's tiny-handle mode without
    // overlapping adjacent panels. Painting remains independently limited to the center pixel.
    setHandleWidth(HandleExtent);
}

VSplitter::~VSplitter() {
    // QSplitter deletes its handles from the base destructor, after VSplitter's members have been
    // destroyed. Detach them while the shared cursor-guard state is still alive.
    for (int index = 1; index < count(); ++index) {
        static_cast<VSplitterHandle*>(handle(index))->detachOwner();
    }
    cursorGuardOwners_.clear();
}

QSplitterHandle* VSplitter::createHandle() {
    return new VSplitterHandle(orientation(), this);
}

void VSplitter::acquireCursorGuard(QSplitterHandle* handle) {
    if (handle == nullptr || cursorGuardOwners_.contains(handle)) {
        return;
    }
    if (cursorGuardOwners_.isEmpty()) {
        hadExplicitCursorBeforeGuard_ = testAttribute(Qt::WA_SetCursor);
        cursorBeforeGuard_ = cursor();
    }
    cursorGuardOwners_.insert(handle);

    // Qt can transiently route an in-bounds point to an adjacent alien widget. Giving every
    // descendant the same inherited cursor prevents that receiver switch from applying Arrow.
    setCursor(handle->cursor());
}

void VSplitter::releaseCursorGuard(QSplitterHandle* handle) {
    if (handle == nullptr || !cursorGuardOwners_.remove(handle) || !cursorGuardOwners_.isEmpty()) {
        return;
    }
    if (hadExplicitCursorBeforeGuard_) {
        setCursor(cursorBeforeGuard_);
    } else {
        unsetCursor();
    }
}

void VSplitter::queueHandleClicked(QSplitterHandle* handle, const Qt::MouseButton button) {
    for (int index = 1; index < count(); ++index) {
        if (this->handle(index) == handle) {
            // Panel mutations can move this handle. Defer them until Qt has released the
            // mouse grab and reconciled the effective hover widget and platform cursor.
            QMetaObject::invokeMethod(
                this, [this, index, button] { emit handleClicked(index, button); },
                Qt::QueuedConnection);
            return;
        }
    }
}

} // namespace vkui
