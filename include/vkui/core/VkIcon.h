// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMetaType>
#include <QtGui/QColor>
#include <QtGui/QIcon>
#include <QtGui/QPalette>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkSymbol.h>

namespace vkui {

enum class VkIconRole {
    Primary,
    Secondary,
    Disabled,
    Accent,
    Destructive,
};

/** Creates a scalable icon whose semantic colors follow the current theme. */
[[nodiscard]] VKUI_CORE_EXPORT QIcon icon(VkSymbol symbol, VkIconRole role = VkIconRole::Primary);

/** Creates an icon that resolves its colors from the application palette when rendered. */
[[nodiscard]] VKUI_CORE_EXPORT QIcon icon(VkSymbol symbol, QPalette::ColorRole role,
                                          QPalette::ColorGroup group = QPalette::Active);

/**
 * Creates a scalable icon with caller-supplied two-tone colors.
 *
 * This overload is intended for local surfaces whose palette can legitimately
 * differ from the process-wide semantic theme. An invalid secondary color is
 * derived as a quieter contrasting tone.
 */
[[nodiscard]] VKUI_CORE_EXPORT QIcon icon(VkSymbol symbol, const QColor& primary,
                                          const QColor& secondary = {});

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VkSymbol)
Q_DECLARE_METATYPE(vkui::VkIconRole)
