// SPDX-License-Identifier: MIT

#include "VkThemeRefreshCoordinator_p.h"

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyle>
#include <QtWidgets/QWidget>
#include <algorithm>
#include <ranges>
#include <utility>
#include <vkui/core/VkThemeManager.h>

namespace vkui {
namespace {

int widgetDepth(const QWidget* widget) {
    int depth = 0;
    for (const QWidget* parent = widget ? widget->parentWidget() : nullptr; parent;
         parent = parent->parentWidget()) {
        ++depth;
    }
    return depth;
}

void updateVisibleWindows() {
    for (QWidget* window : QApplication::topLevelWidgets()) {
        if (window && window->isVisible()) {
            window->update();
        }
    }
}

} // namespace

VkThemeRefreshCoordinator::VkThemeRefreshCoordinator(QApplication& application)
    : QObject(&application), application_(&application) {
    connect(VkThemeManager::instance(), &VkThemeManager::themeChanged, this,
            [this](quint64, const VkThemeChanges changes) { scheduleRefresh(changes); });
}

void VkThemeRefreshCoordinator::scheduleRefresh(const VkThemeChanges changes) {
    const VkThemeChanges relevant =
        changes & (VkThemeChange::Colors | VkThemeChange::Metrics | VkThemeChange::Typography);
    if (!relevant) {
        return;
    }

    pendingChanges_ |= relevant;
    if (refreshPending_) {
        return;
    }
    refreshPending_ = true;

    // Coalesce related token updates and avoid mutating widget state while a popup is dispatching
    // the input event that selected the new theme.
    QTimer::singleShot(0, this, [this] { refreshWidgets(); });
}

void VkThemeRefreshCoordinator::refreshWidgets() {
    refreshPending_ = false;
    const VkThemeChanges changes = std::exchange(pendingChanges_, {});
    QApplication* application = application_.data();
    if (!application) {
        return;
    }

    if (changes.testFlag(VkThemeChange::Typography)) {
        // VkThemeManager applies the resolved body font through QGuiApplication. Qt then owns
        // inherited-font resolution, FontChange delivery, size-hint invalidation, and layout
        // activation. Repeating that work through QApplication::allWidgets() would be both
        // redundant and observably slower while a text-size slider is moving.
        updateVisibleWindows();
        return;
    }

    if (!changes.testFlag(VkThemeChange::Metrics)) {
        // QApplication::setPalette already propagates palette changes. One update per visible
        // top-level surface also covers tokens that are consumed directly by custom painters.
        updateVisibleWindows();
        return;
    }

    struct WidgetEntry final {
        QPointer<QWidget> widget;
        int depth = 0;
    };

    QList<WidgetEntry> widgets;
    const QWidgetList liveWidgets = QApplication::allWidgets();
    widgets.reserve(liveWidgets.size());
    for (QWidget* widget : liveWidgets) {
        widgets.append({widget, widgetDepth(widget)});
    }

    // Structural changes are rare. Preserve Qt's parent/child polish ordering so inherited fonts,
    // palettes, metrics, and size hints are rebuilt deterministically.
    std::ranges::sort(widgets, [](const WidgetEntry& left, const WidgetEntry& right) {
        return left.depth > right.depth;
    });
    for (const WidgetEntry& entry : widgets) {
        const QPointer<QWidget>& widget = entry.widget;
        if (widget && widget->testAttribute(Qt::WA_WState_Polished) && widget->style()) {
            widget->style()->unpolish(widget);
        }
    }

    std::ranges::reverse(widgets);
    for (const WidgetEntry& entry : widgets) {
        const QPointer<QWidget>& widget = entry.widget;
        if (!widget) {
            continue;
        }
        if (widget->testAttribute(Qt::WA_WState_Polished) && widget->style()) {
            widget->style()->polish(widget);
        }
        widget->updateGeometry();
        widget->update();
    }

    updateVisibleWindows();
}

} // namespace vkui
