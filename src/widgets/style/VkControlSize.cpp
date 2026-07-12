// SPDX-License-Identifier: MIT

#include <QVariant>
#include <QWidget>
#include <algorithm>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VkControlSize.h>
#include <vkui/widgets/controls/VkSwitch.h>

namespace {

constexpr auto controlSizeProperty = "_vkui_controlSize";
constexpr auto controlExtentProperty = "_vkui_controlExtent";

bool isValidControlSize(int value) noexcept {
    return value >= static_cast<int>(vkui::VkControlSize::Small) &&
           value <= static_cast<int>(vkui::VkControlSize::Large);
}

} // namespace

namespace vkui {

int controlExtent(VkControlSize size) noexcept {
    const auto& metrics = VkThemeManager::instance()->theme().metrics();
    switch (size) {
    case VkControlSize::Small:
        return qRound(metrics.fixedControlExtentSmall);
    case VkControlSize::Large:
        return qRound(metrics.fixedControlExtentLarge);
    case VkControlSize::Regular:
    default:
        return qRound(metrics.fixedControlExtentRegular);
    }
}

VkControlSize controlSize(const QWidget& widget) noexcept {
    const QVariant value = widget.property(controlSizeProperty);
    if (!value.isValid() || !isValidControlSize(value.toInt())) {
        return VkControlSize::Regular;
    }
    return static_cast<VkControlSize>(value.toInt());
}

void setControlSize(QWidget& widget, VkControlSize size) {
    if (auto* control = qobject_cast<VkSwitch*>(&widget)) {
        control->setControlSize(size);
        return;
    }
    if (controlSize(widget) == size && widget.property(controlSizeProperty).isValid() &&
        !customControlExtent(widget)) {
        return;
    }

    widget.setProperty(controlSizeProperty, static_cast<int>(size));
    widget.setProperty(controlExtentProperty, QVariant{});
    widget.updateGeometry();
    widget.update();
}

std::optional<int> customControlExtent(const QWidget& widget) noexcept {
    const QVariant value = widget.property(controlExtentProperty);
    if (!value.isValid()) {
        return std::nullopt;
    }
    return std::clamp(value.toInt(), VkMinimumControlExtent, VkMaximumControlExtent);
}

int controlExtent(const QWidget& widget) noexcept {
    if (const auto custom = customControlExtent(widget)) {
        return *custom;
    }
    return controlExtent(controlSize(widget));
}

void setControlExtent(QWidget& widget, int logicalPixels) {
    if (auto* control = qobject_cast<VkSwitch*>(&widget)) {
        control->setControlExtent(logicalPixels);
        return;
    }
    const int normalized =
        std::clamp(logicalPixels, VkMinimumControlExtent, VkMaximumControlExtent);
    if (customControlExtent(widget) == normalized) {
        return;
    }
    widget.setProperty(controlExtentProperty, normalized);
    widget.updateGeometry();
    widget.update();
}

void resetControlExtent(QWidget& widget) {
    if (auto* control = qobject_cast<VkSwitch*>(&widget)) {
        control->resetControlExtent();
        return;
    }
    if (!customControlExtent(widget)) {
        return;
    }
    widget.setProperty(controlExtentProperty, QVariant{});
    widget.updateGeometry();
    widget.update();
}

} // namespace vkui
