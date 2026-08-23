// SPDX-License-Identifier: MIT

#include <QVariant>
#include <QWidget>
#include <algorithm>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VControlSize.h>
#include <vkui/widgets/controls/VSwitch.h>

namespace {

constexpr auto controlSizeProperty = "_vkui_controlSize";
constexpr auto controlExtentProperty = "_vkui_controlExtent";

bool isValidControlSize(int value) noexcept {
    return value >= static_cast<int>(vkui::VControlSize::Small) &&
           value <= static_cast<int>(vkui::VControlSize::Large);
}

} // namespace

namespace vkui {

int controlExtent(VControlSize size) noexcept {
    const auto& metrics = VkThemeManager::instance()->theme().metrics();
    switch (size) {
    case VControlSize::Small:
        return qRound(metrics.fixedControlExtentSmall);
    case VControlSize::Large:
        return qRound(metrics.fixedControlExtentLarge);
    case VControlSize::Regular:
    default:
        return qRound(metrics.fixedControlExtentRegular);
    }
}

VControlSize controlSize(const QWidget& widget) noexcept {
    const QVariant value = widget.property(controlSizeProperty);
    if (!value.isValid() || !isValidControlSize(value.toInt())) {
        return VControlSize::Regular;
    }
    return static_cast<VControlSize>(value.toInt());
}

void setControlSize(QWidget& widget, VControlSize size) {
    if (auto* control = qobject_cast<VSwitch*>(&widget)) {
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
    return std::clamp(value.toInt(), VMinimumControlExtent, VMaximumControlExtent);
}

int controlExtent(const QWidget& widget) noexcept {
    if (const auto custom = customControlExtent(widget)) {
        return *custom;
    }
    return controlExtent(controlSize(widget));
}

void setControlExtent(QWidget& widget, int logicalPixels) {
    if (auto* control = qobject_cast<VSwitch*>(&widget)) {
        control->setControlExtent(logicalPixels);
        return;
    }
    const int normalized =
        std::clamp(logicalPixels, VMinimumControlExtent, VMaximumControlExtent);
    if (customControlExtent(widget) == normalized) {
        return;
    }
    widget.setProperty(controlExtentProperty, normalized);
    widget.updateGeometry();
    widget.update();
}

void resetControlExtent(QWidget& widget) {
    if (auto* control = qobject_cast<VSwitch*>(&widget)) {
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
