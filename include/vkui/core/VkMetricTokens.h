// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QtTypes>
#include <vkui/VkUiGlobal.h>

namespace vkui {

/** Logical, device-independent geometry values shared by vkui components. */
struct VKUI_CORE_EXPORT VkMetricTokens final {
    qreal spacing2 = 0.0;
    qreal spacing4 = 0.0;
    qreal spacing6 = 0.0;
    qreal spacing8 = 0.0;
    qreal spacing12 = 0.0;
    qreal spacing16 = 0.0;
    qreal spacing20 = 0.0;
    qreal spacing24 = 0.0;

    qreal controlHeightSmall = 0.0;
    qreal controlHeightRegular = 0.0;
    qreal controlHeightLarge = 0.0;

    // Named fixed-proportion controls resolve to these logical-pixel extents.
    qreal fixedControlExtentSmall = 0.0;
    qreal fixedControlExtentRegular = 0.0;
    qreal fixedControlExtentLarge = 0.0;

    qreal cornerRadiusSmall = 0.0;
    qreal cornerRadiusRegular = 0.0;
    qreal cornerRadiusLarge = 0.0;
    qreal popoverCornerRadius = 0.0;

    qreal borderWidth = 0.0;
    qreal focusRingWidth = 0.0;

    qreal switchTrackWidth = 0.0;
    qreal switchTrackHeight = 0.0;
    qreal switchThumbDiameter = 0.0;

    qreal popoverArrowWidth = 0.0;
    qreal popoverArrowDepth = 0.0;
    qreal popoverScreenMargin = 0.0;
    qreal popoverAnchorGap = 0.0;
    qreal popoverShadowRadius = 0.0;
};

} // namespace vkui
