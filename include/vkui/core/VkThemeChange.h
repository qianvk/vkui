// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QFlags>
#include <QtCore/QMetaType>
#include <QtCore/QtTypes>

namespace vkui {

/** Describes which immutable theme token groups changed. Flags can be combined. */
enum class VkThemeChange : quint8 {
    None = 0,
    Colors = 1U << 0U,
    Metrics = 1U << 1U,
    Typography = 1U << 2U,
    Motion = 1U << 3U,
};
Q_DECLARE_FLAGS(VkThemeChanges, VkThemeChange)

} // namespace vkui

Q_DECLARE_OPERATORS_FOR_FLAGS(vkui::VkThemeChanges)
Q_DECLARE_METATYPE(vkui::VkThemeChange)
Q_DECLARE_METATYPE(vkui::VkThemeChanges)
