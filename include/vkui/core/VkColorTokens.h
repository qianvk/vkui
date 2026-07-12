// SPDX-License-Identifier: MIT

#pragma once

#include <QtGui/QColor>
#include <vkui/VkUiGlobal.h>

namespace vkui {

/** Semantic colors for one fully resolved appearance. */
struct VKUI_CORE_EXPORT VkColorTokens final {
    QColor windowBackground;
    QColor contentBackground;
    QColor elevatedBackground;
    QColor popoverBackground;

    QColor controlFill;
    QColor controlFillHovered;
    QColor controlFillPressed;
    QColor controlFillDisabled;

    QColor separator;
    QColor border;
    QColor borderStrong;

    QColor textPrimary;
    QColor textSecondary;
    QColor textTertiary;
    QColor textDisabled;

    QColor accent;
    QColor accentHovered;
    QColor accentPressed;

    QColor destructive;
    QColor warning;
    QColor success;

    QColor focusRing;
    QColor shadow;

    QColor symbolPrimary;
    QColor symbolSecondary;
    QColor symbolDisabled;
};

} // namespace vkui
