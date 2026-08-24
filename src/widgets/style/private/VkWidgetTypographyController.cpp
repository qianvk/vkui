// SPDX-License-Identifier: MIT

#include "VkWidgetTypographyController_p.h"

#include <QtCore/QEvent>
#include <QtWidgets/QWidget>
#include <vkui/core/VkThemeManager.h>

namespace vkui {

VkWidgetTypographyController::VkWidgetTypographyController(QObject* parent) : QObject(parent) {}

VkWidgetTypographyController::~VkWidgetTypographyController() {
    restoreAll();
}

void VkWidgetTypographyController::polish(QWidget* widget) {
    if (widget == nullptr) {
        return;
    }

    auto iterator = widgets_.find(widget);
    if (iterator == widgets_.end()) {
        WidgetState state;
        state.isWindow = widget->isWindow();
        state.originalWindowPropagation =
            state.isWindow && widget->testAttribute(Qt::WA_WindowPropagation);
        state.managesFont = !hasApplicationFontOverride(widget);
        iterator = widgets_.insert(widget, state);
        widget->installEventFilter(this);
        connect(widget, &QObject::destroyed, this, [this, widget] { widgets_.remove(widget); });
    }

    WidgetState& state = iterator.value();
    if (state.isWindow) {
        widget->setAttribute(Qt::WA_WindowPropagation, true);
    }
    if (!state.managesFont && !hasApplicationFontOverride(widget)) {
        state.managesFont = true;
    }
    if (state.managesFont) {
        applyThemeFont(*widget, state);
    }
}

void VkWidgetTypographyController::restoreAll() {
    const auto widgets = widgets_.keys();
    for (QWidget* widget : widgets) {
        auto iterator = widgets_.find(widget);
        if (iterator == widgets_.end() || widget == nullptr) {
            continue;
        }
        WidgetState& state = iterator.value();
        widget->removeEventFilter(this);
        if (state.managesFont && state.hasAppliedFont && widget->font() == state.appliedFont) {
            state.applyingFont = true;
            widget->setFont(QFont{});
            state.applyingFont = false;
        }
        if (state.isWindow) {
            widget->setAttribute(Qt::WA_WindowPropagation, state.originalWindowPropagation);
        }
    }
    widgets_.clear();
}

bool VkWidgetTypographyController::eventFilter(QObject* watched, QEvent* event) {
    if (event == nullptr || event->type() != QEvent::FontChange) {
        return QObject::eventFilter(watched, event);
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    auto iterator = widgets_.find(widget);
    if (iterator == widgets_.end() || iterator->applyingFont) {
        return QObject::eventFilter(watched, event);
    }

    WidgetState& state = iterator.value();
    const QFont themeFont = VkThemeManager::instance()->theme().typography().body;
    if (state.managesFont && widget->font() == themeFont) {
        state.appliedFont = widget->font();
        state.hasAppliedFont = true;
        return QObject::eventFilter(watched, event);
    }

    if (state.managesFont) {
        // A non-theme font assigned outside the controller becomes application-owned. Release
        // managed descendants so QWidget inheritance applies that override transitively.
        state.managesFont = false;
        state.hasAppliedFont = false;
        releaseManagedDescendants(*widget);
    } else if (!hasApplicationFontOverride(widget)) {
        state.managesFont = true;
        applyThemeFont(*widget, state);
    }
    return QObject::eventFilter(watched, event);
}

bool VkWidgetTypographyController::hasApplicationFontOverride(const QWidget* widget) const {
    for (const QWidget* candidate = widget; candidate; candidate = candidate->parentWidget()) {
        if (!candidate->testAttribute(Qt::WA_SetFont)) {
            continue;
        }
        const auto iterator = widgets_.constFind(const_cast<QWidget*>(candidate));
        const bool controllerOwned = iterator != widgets_.cend() && iterator->managesFont &&
                                     iterator->hasAppliedFont &&
                                     candidate->font() == iterator->appliedFont;
        if (!controllerOwned) {
            return true;
        }
    }
    return false;
}

void VkWidgetTypographyController::applyThemeFont(QWidget& widget, WidgetState& state) {
    const QFont target = VkThemeManager::instance()->theme().typography().body;
    state.applyingFont = true;
    widget.setFont(target);
    state.applyingFont = false;
    state.appliedFont = widget.font();
    state.hasAppliedFont = true;
}

void VkWidgetTypographyController::releaseManagedDescendants(QWidget& root) {
    for (auto iterator = widgets_.begin(); iterator != widgets_.end(); ++iterator) {
        QWidget* descendant = iterator.key();
        WidgetState& state = iterator.value();
        if (descendant == nullptr || descendant == &root || !root.isAncestorOf(descendant) ||
            !state.managesFont) {
            continue;
        }
        state.applyingFont = true;
        descendant->setFont(QFont{});
        state.applyingFont = false;
        state.managesFont = false;
        state.hasAppliedFont = false;
    }
}

} // namespace vkui
