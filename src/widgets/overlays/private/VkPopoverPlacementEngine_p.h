// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMargins>
#include <QtCore/QPointF>
#include <QtCore/QRect>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>
#include <QtCore/Qt>
#include <vkui/widgets/overlays/VkPopover.h>

namespace vkui {

/** Input for the deterministic, QWidget-independent placement calculation. */
struct VkPopoverPlacementInput final {
    QRectF anchorRect;
    QSizeF contentSize;
    VkPopoverPlacement preferredPlacement = VkPopoverPlacement::Automatic;
    QRectF availableGeometry;
    qreal screenMargin = 0.0;
    qreal anchorGap = 0.0;
    qreal bodyCornerRadius = 0.0;
    qreal arrowWidth = 0.0;
    qreal arrowDepth = 0.0;
    Qt::LayoutDirection layoutDirection = Qt::LeftToRight;

    // These implementation-facing insets do not affect placement policy. They
    // let the widget reserve content padding and transparent shadow pixels
    // while keeping the geometry engine independently testable.
    QMarginsF contentMargins;
    qreal outerMargin = 0.0;
};

/**
 * Result of a placement calculation.
 *
 * popupRect is in global coordinates. Every other geometry is local to
 * popupRect, which makes the result directly usable by painting and layout.
 */
struct VkPopoverPlacementResult final {
    VkPopoverPlacement resolvedPlacement = VkPopoverPlacement::Automatic;
    QRect popupRect;
    QRectF bodyRect;
    QRectF contentRect;
    QPointF arrowTip;
    QPointF arrowBaseCenter;
    bool valid = false;

    [[nodiscard]] bool isValid() const noexcept {
        return valid;
    }
};

/** Pure placement policy for VkPopover. */
class VkPopoverPlacementEngine final {
  public:
    [[nodiscard]] static VkPopoverPlacementResult calculate(const VkPopoverPlacementInput& input);

    [[nodiscard]] static VkPopoverPlacementResult place(const VkPopoverPlacementInput& input) {
        return calculate(input);
    }
};

} // namespace vkui
