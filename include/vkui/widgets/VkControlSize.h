// SPDX-License-Identifier: MIT

#pragma once

#include <QMetaType>
#include <optional>
#include <vkui/VkUiGlobal.h>

class QWidget;

namespace vkui {

/** Platform-style size classes for controls with fixed visual proportions. */
enum class VkControlSize {
    Small,
    Regular,
    Large,
};

inline constexpr int VkMinimumControlExtent = 8;
inline constexpr int VkMaximumControlExtent = 128;

/** Resolves a named size token to its logical-pixel visual extent. */
VKUI_WIDGETS_EXPORT int controlExtent(VkControlSize size) noexcept;

/** Returns the style size assigned to a widget, or Regular when unset. */
VKUI_WIDGETS_EXPORT VkControlSize controlSize(const QWidget& widget) noexcept;

/**
 * Assigns a style size and invalidates only the affected widget's geometry.
 *
 * QCheckBox and QRadioButton use this property through VkStyle. VkSwitch also
 * exposes the same value as a native Qt property and member API.
 */
VKUI_WIDGETS_EXPORT void setControlSize(QWidget& widget, VkControlSize size);

/** Returns a widget's exact override, or no value when it follows its preset. */
VKUI_WIDGETS_EXPORT std::optional<int> customControlExtent(const QWidget& widget) noexcept;

/** Returns the effective logical-pixel extent after applying preset and override. */
VKUI_WIDGETS_EXPORT int controlExtent(const QWidget& widget) noexcept;

/** Sets an exact logical-pixel extent, clamped to the supported safe range. */
VKUI_WIDGETS_EXPORT void setControlExtent(QWidget& widget, int logicalPixels);

/** Removes the exact override and restores the currently selected preset. */
VKUI_WIDGETS_EXPORT void resetControlExtent(QWidget& widget);

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VkControlSize)
