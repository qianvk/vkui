// SPDX-License-Identifier: MIT

#pragma once

#include <QMetaType>
#include <optional>
#include <vkui/VkUiGlobal.h>

class QWidget;

namespace vkui {

/** Platform-style size classes whose presets follow the application text scale. */
enum class VControlSize {
    Small,
    Regular,
    Large,
};

inline constexpr int VMinimumControlExtent = 8;
inline constexpr int VMaximumControlExtent = 128;

/** Resolves a named size token to its current text-responsive logical-pixel extent. */
VKUI_WIDGETS_EXPORT int controlExtent(VControlSize size) noexcept;

/** Returns the style size assigned to a widget, or Regular when unset. */
VKUI_WIDGETS_EXPORT VControlSize controlSize(const QWidget& widget) noexcept;

/**
 * Assigns a style size and invalidates only the affected widget's geometry.
 *
 * QCheckBox and QRadioButton use this property through VStyle. VSwitch also
 * exposes the same value as a native Qt property and member API.
 */
VKUI_WIDGETS_EXPORT void setControlSize(QWidget& widget, VControlSize size);

/** Returns a widget's exact override, or no value when it follows its preset. */
VKUI_WIDGETS_EXPORT std::optional<int> customControlExtent(const QWidget& widget) noexcept;

/** Returns the effective logical-pixel extent after applying preset and override. */
VKUI_WIDGETS_EXPORT int controlExtent(const QWidget& widget) noexcept;

/** Sets an absolute logical-pixel extent that remains stable across text-scale changes. */
VKUI_WIDGETS_EXPORT void setControlExtent(QWidget& widget, int logicalPixels);

/** Removes the exact override and restores the currently selected preset. */
VKUI_WIDGETS_EXPORT void resetControlExtent(QWidget& widget);

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VControlSize)
