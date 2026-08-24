// SPDX-License-Identifier: MIT

#include "VkPopupSurfaceStyler_p.h"

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QPainter>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QWidget>
#include <algorithm>
#include <ranges>
#include <vkui/widgets/VCombobox.h>

namespace vkui {

VkPopupSurfaceStyler::VkPopupSurfaceStyler(QObject* parent) : QObject(parent) {}

VkPopupSurfaceStyler::~VkPopupSurfaceStyler() {
    const auto popups = popups_.keys();
    for (QWidget* popup : popups) {
        unpolish(popup);
    }
}

bool VkPopupSurfaceStyler::isComboBoxPopup(const QWidget* widget) {
    return widget && widget->inherits("QComboBoxPrivateContainer");
}

bool VkPopupSurfaceStyler::isVComboboxPopup(const QWidget* widget) {
    return isComboBoxPopup(widget) && owningVCombobox(widget) != nullptr;
}

const VCombobox* VkPopupSurfaceStyler::owningVCombobox(const QWidget* widget) {
    for (const QWidget* candidate = widget; candidate; candidate = candidate->parentWidget()) {
        if (const auto* comboBox = qobject_cast<const VCombobox*>(candidate)) {
            return comboBox;
        }
    }
    return nullptr;
}

bool VkPopupSurfaceStyler::isMenuPopup(const QWidget* widget) {
    return widget && widget->inherits("QMenu");
}

bool VkPopupSurfaceStyler::isPopupContainer(const QWidget* widget) {
    return isVComboboxPopup(widget) || isMenuPopup(widget);
}

bool VkPopupSurfaceStyler::isPopupPart(const QWidget* widget) const {
    for (const QWidget* candidate = widget; candidate; candidate = candidate->parentWidget()) {
        if (isPopupContainer(candidate)) {
            return true;
        }
    }
    return false;
}

void VkPopupSurfaceStyler::polish(QWidget* widget) {
    if (!isPopupContainer(widget) || popups_.contains(widget)) {
        return;
    }

    PopupState state;
    state.translucentBackground = widget->testAttribute(Qt::WA_TranslucentBackground);
    state.noSystemBackground = widget->testAttribute(Qt::WA_NoSystemBackground);
    state.opaquePaintEvent = widget->testAttribute(Qt::WA_OpaquePaintEvent);
    state.styledBackground = widget->testAttribute(Qt::WA_StyledBackground);
    state.autoFillBackground = widget->autoFillBackground();
    state.palette = widget->palette();
    state.mask = widget->mask();
    state.windowFlags = widget->windowFlags();
    popups_.insert(widget, state);

    widget->setAttribute(Qt::WA_TranslucentBackground, true);
    widget->setAttribute(Qt::WA_NoSystemBackground, true);
    widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
    widget->setAttribute(Qt::WA_StyledBackground, false);
    widget->setAutoFillBackground(false);
    applyTransparentPalette(*widget);
    widget->clearMask();
#if defined(Q_OS_WIN)
    // Qt requires a frameless top-level window for translucent QWidget backgrounds on Windows.
    widget->setWindowFlag(Qt::FramelessWindowHint, true);
    widget->setWindowFlag(Qt::NoDropShadowWindowHint, true);
#endif
    widget->installEventFilter(this);
    connect(widget, &QObject::destroyed, this, [this, widget] { popups_.remove(widget); });
    widget->update();
}

void VkPopupSurfaceStyler::unpolish(QWidget* widget) {
    auto iterator = popups_.find(widget);
    if (iterator == popups_.end()) {
        return;
    }
    const PopupState state = iterator.value();
    popups_.erase(iterator);

    widget->removeEventFilter(this);
    widget->setAttribute(Qt::WA_TranslucentBackground, state.translucentBackground);
    widget->setAttribute(Qt::WA_NoSystemBackground, state.noSystemBackground);
    widget->setAttribute(Qt::WA_OpaquePaintEvent, state.opaquePaintEvent);
    widget->setAttribute(Qt::WA_StyledBackground, state.styledBackground);
    widget->setAutoFillBackground(state.autoFillBackground);
    widget->setPalette(state.palette);
#if defined(Q_OS_WIN)
    widget->setWindowFlags(state.windowFlags);
#endif
    if (state.mask.isEmpty()) {
        widget->clearMask();
    } else {
        widget->setMask(state.mask);
    }
}

bool VkPopupSurfaceStyler::eventFilter(QObject* watched, QEvent* event) {
    auto* popup = qobject_cast<QWidget*>(watched);
    if (!popup || !popups_.contains(popup)) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::Paint: {
        // Popup backing stores are reused, so clear stale alpha before Qt paints the next frame.
        QPainter clearPainter(popup);
        clearPainter.setCompositionMode(QPainter::CompositionMode_Source);
        clearPainter.fillRect(popup->rect(), Qt::transparent);
        break;
    }
    case QEvent::Resize:
    case QEvent::ChildAdded:
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
        applyTransparentPalette(*popup);
        popup->clearMask();
        popup->update();
        break;
    case QEvent::Show:
        applyTransparentPalette(*popup);
        popup->clearMask();
        popup->update();
        if (auto* menu = qobject_cast<QMenu*>(popup)) {
            scheduleMenuStackRestore(menu, true);
        }
        break;
    case QEvent::MouseButtonPress:
    case QEvent::ZOrderChange:
        if (auto* menu = qobject_cast<QMenu*>(popup)) {
            scheduleMenuStackRestore(menu, false);
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void VkPopupSurfaceStyler::raiseVisibleSubmenuChain(QMenu* menu) {
    if (!menu || !menu->isVisible()) {
        return;
    }
    for (QAction* action : menu->actions()) {
        QMenu* submenu = action ? action->menu() : nullptr;
        if (!submenu || !submenu->isVisible()) {
            continue;
        }
        submenu->raise();
        raiseVisibleSubmenuChain(submenu);
    }
}

bool VkPopupSurfaceStyler::hasMenuTransientParent(const QMenu* menu) {
    const QWindow* popupWindow = menu ? menu->windowHandle() : nullptr;
    const QWindow* transientParent = popupWindow ? popupWindow->transientParent() : nullptr;
    if (!transientParent) {
        return false;
    }
    return std::ranges::any_of(
        QApplication::topLevelWidgets(), [transientParent](const QWidget* widget) {
            return qobject_cast<const QMenu*>(widget) && widget->windowHandle() == transientParent;
        });
}

void VkPopupSurfaceStyler::scheduleMenuStackRestore(QMenu* menu, const bool raiseMenu) {
    if (!menu) {
        return;
    }
    const QPointer<QMenu> guardedMenu(menu);
    QTimer::singleShot(0, menu, [guardedMenu, raiseMenu] {
        if (!guardedMenu || !guardedMenu->isVisible()) {
            return;
        }
        if (raiseMenu && hasMenuTransientParent(guardedMenu)) {
            guardedMenu->raise();
        }
        raiseVisibleSubmenuChain(guardedMenu);
    });
}

void VkPopupSurfaceStyler::applyTransparentPalette(QWidget& widget) {
    QPalette transparent = widget.palette();
    transparent.setColor(QPalette::Base, Qt::transparent);
    transparent.setColor(QPalette::AlternateBase, Qt::transparent);
    transparent.setColor(QPalette::Window, Qt::transparent);
    widget.setPalette(transparent);
}

} // namespace vkui
