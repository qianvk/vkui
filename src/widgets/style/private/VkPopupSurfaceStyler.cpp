// SPDX-License-Identifier: MIT

#include "VkPopupSurfaceStyler_p.h"

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QPainter>
#include <QtGui/QWindow>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QWidget>
#include <algorithm>
#include <ranges>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VCombobox.h>
#include <vkui/widgets/effects/VLiquidGlass.h>

#include "VStylePainter_p.h"

namespace {

qreal deviceHairlineWidth(const QPainter& painter) {
    const QPaintDevice* device = painter.device();
    return 1.0 / std::max<qreal>(1.0, device ? device->devicePixelRatioF() : 1.0);
}

} // namespace

namespace vkui {

VkPopupSurfaceStyler::VkPopupSurfaceStyler(QObject* parent) : QObject(parent) {
    const auto refreshGlassMaterials = [this](const bool synchronizeSurfaces) {
        const auto popups = popups_.keys();
        for (QWidget* popup : popups) {
            if (!popup) {
                continue;
            }
            if (synchronizeSurfaces) {
                syncLiquidGlassSurface(*popup);
            }
            const auto iterator = popups_.find(popup);
            if (iterator != popups_.end() && iterator->glassBackdrop) {
                iterator->glassBackdrop->invalidate();
            } else {
                popup->update();
            }
        }
    };
    connect(VkThemeManager::instance(), &VkThemeManager::liquidGlassEnabledChanged, this,
            [refreshGlassMaterials](bool) { refreshGlassMaterials(true); });
    connect(VkThemeManager::instance(), &VkThemeManager::liquidGlassTintLevelChanged, this,
            [refreshGlassMaterials](int) { refreshGlassMaterials(false); });
}

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

void VkPopupSurfaceStyler::drawPopupSurface(const QWidget& popup, QPainter& painter) const {
    const VkTheme& theme = VkThemeManager::instance()->theme();
    const VkMetricTokens& metrics = theme.metrics();
    const bool comboBoxPopup = isVComboboxPopup(&popup);
    const qreal radius =
        comboBoxPopup ? metrics.comboBoxPopupCornerRadius : metrics.menuCornerRadius;

    const auto iterator = popups_.constFind(const_cast<QWidget*>(&popup));
    if (VkThemeManager::instance()->liquidGlassEnabled() && iterator != popups_.cend() &&
        iterator->glassSurface) {
        iterator->glassSurface->paintMaterial(painter);
        return;
    }

    const QColor border = VStylePainter::multiplyAlpha(theme.colors().border, 0.68);
    VStylePainter::drawRoundedPanel(painter, QRectF(popup.rect()), radius,
                                    theme.colors().elevatedBackground, border,
                                    deviceHairlineWidth(painter));
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
    PopupState& storedState = popups_[widget];

    widget->setAttribute(Qt::WA_TranslucentBackground, true);
    widget->setAttribute(Qt::WA_NoSystemBackground, true);
    widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
    widget->setAttribute(Qt::WA_StyledBackground, false);
    widget->setAutoFillBackground(false);
    applyTransparentPalette(*widget);
    syncTransparentContent(*widget, storedState);
    widget->clearMask();
#if defined(Q_OS_WIN)
    // Qt requires a frameless top-level window for translucent QWidget backgrounds on Windows.
    widget->setWindowFlag(Qt::FramelessWindowHint, true);
    widget->setWindowFlag(Qt::NoDropShadowWindowHint, true);
#endif
    widget->installEventFilter(this);
    connect(widget, &QObject::destroyed, this, [this, widget] { popups_.remove(widget); });
    syncLiquidGlassSurface(*widget);
    widget->update();
}

void VkPopupSurfaceStyler::unpolish(QWidget* widget) {
    auto iterator = popups_.find(widget);
    if (iterator == popups_.end()) {
        return;
    }
    const PopupState state = iterator.value();
    popups_.erase(iterator);

    delete state.glassSurface;
    delete state.glassBackdrop;
    restoreContentWidgets(state);
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
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
        applyTransparentPalette(*popup);
        syncTransparentContent(*popup, popups_[popup]);
        popup->clearMask();
        syncLiquidGlassSurface(*popup);
        popup->update();
        break;
    case QEvent::ChildAdded: {
        applyTransparentPalette(*popup);
        popup->clearMask();
        const QPointer<QWidget> guardedPopup(popup);
        QTimer::singleShot(0, this, [this, guardedPopup] {
            if (guardedPopup && popups_.contains(guardedPopup)) {
                syncTransparentContent(*guardedPopup, popups_[guardedPopup]);
                syncLiquidGlassSurface(*guardedPopup);
                guardedPopup->update();
            }
        });
        break;
    }
    case QEvent::Show:
        applyTransparentPalette(*popup);
        syncTransparentContent(*popup, popups_[popup]);
        popup->clearMask();
        syncLiquidGlassSurface(*popup);
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

void VkPopupSurfaceStyler::makeContentWidgetTransparent(QWidget& widget, PopupState& state) {
    const auto alreadyStored = std::ranges::any_of(
        state.contentWidgets,
        [&widget](const ContentWidgetState& item) { return item.widget == &widget; });
    if (!alreadyStored) {
        state.contentWidgets.append({
            &widget,
            widget.testAttribute(Qt::WA_NoSystemBackground),
            widget.testAttribute(Qt::WA_OpaquePaintEvent),
            widget.testAttribute(Qt::WA_StyledBackground),
            widget.autoFillBackground(),
            widget.backgroundRole(),
            widget.palette(),
        });
    }

    // QAbstractItemView assigns QPalette::Base to its viewport. Disable every erase path so the
    // popup's single rounded surface remains visible through the complete list rectangle.
    widget.setAttribute(Qt::WA_NoSystemBackground, true);
    widget.setAttribute(Qt::WA_OpaquePaintEvent, false);
    widget.setAttribute(Qt::WA_StyledBackground, false);
    widget.setAutoFillBackground(false);
    widget.setBackgroundRole(QPalette::NoRole);
    applyTransparentPalette(widget);
}

void VkPopupSurfaceStyler::syncTransparentContent(QWidget& popup, PopupState& state) {
    if (!isVComboboxPopup(&popup)) {
        return;
    }
    const auto views = popup.findChildren<QAbstractItemView*>();
    for (QAbstractItemView* view : views) {
        makeContentWidgetTransparent(*view, state);
        if (QWidget* viewport = view->viewport()) {
            makeContentWidgetTransparent(*viewport, state);
        }
    }
}

void VkPopupSurfaceStyler::restoreContentWidgets(const PopupState& state) {
    for (const ContentWidgetState& item : state.contentWidgets) {
        QWidget* widget = item.widget;
        if (!widget) {
            continue;
        }
        widget->setAttribute(Qt::WA_NoSystemBackground, item.noSystemBackground);
        widget->setAttribute(Qt::WA_OpaquePaintEvent, item.opaquePaintEvent);
        widget->setAttribute(Qt::WA_StyledBackground, item.styledBackground);
        widget->setAutoFillBackground(item.autoFillBackground);
        widget->setBackgroundRole(item.backgroundRole);
        widget->setPalette(item.palette);
    }
}

QWidget* VkPopupSurfaceStyler::backdropSourceFor(QWidget& popup) {
    if (const auto* comboBox = owningVCombobox(&popup)) {
        return comboBox->window();
    }

    for (QObject* parent = popup.parent(); parent; parent = parent->parent()) {
        auto* parentWidget = qobject_cast<QWidget*>(parent);
        if (!parentWidget) {
            continue;
        }
        QWidget* candidate = parentWidget->window();
        if (candidate != &popup && !isPopupContainer(candidate)) {
            return candidate;
        }
    }
    QWidget* activeWindow = QApplication::activeWindow();
    return activeWindow != &popup ? activeWindow : nullptr;
}

void VkPopupSurfaceStyler::syncLiquidGlassSurface(QWidget& popup) {
    auto iterator = popups_.find(&popup);
    if (iterator == popups_.end()) {
        return;
    }
    PopupState& state = iterator.value();
    const bool enabled = VkThemeManager::instance()->liquidGlassEnabled();
    if (!enabled) {
        if (state.glassSurface) {
            state.glassSurface->hide();
        }
        return;
    }

    QWidget* source = backdropSourceFor(popup);
    if (!state.glassBackdrop) {
        state.glassBackdrop = new VLiquidGlassBackdrop(source, &popup);
        connect(state.glassBackdrop, &VLiquidGlassBackdrop::invalidated, &popup,
                [&popup] { popup.update(); });
    } else {
        state.glassBackdrop->setSourceWidget(source);
    }
    if (!state.glassSurface) {
        state.glassSurface = new VLiquidGlassSurface(&popup);
        state.glassSurface->setObjectName(QStringLiteral("vkuiPopupLiquidGlassSurface"));
        state.glassSurface->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        state.glassSurface->setFocusPolicy(Qt::NoFocus);
        state.glassSurface->setBackdrop(state.glassBackdrop);
        // This widget only owns the cached material. PE_PanelMenu composites it before Qt paints
        // menu actions or item-view children, so it must never become a visual child overlay.
        state.glassSurface->hide();
    }

    VLiquidGlassStyle style = VLiquidGlassStyle::regular();
    const VkMetricTokens& metrics = VkThemeManager::instance()->theme().metrics();
    style.cornerRadius =
        isVComboboxPopup(&popup) ? metrics.comboBoxPopupCornerRadius : metrics.menuCornerRadius;
    state.glassSurface->setGlassStyle(style);
    state.glassSurface->setGeometry(popup.rect());
    state.glassSurface->hide();
}

} // namespace vkui
