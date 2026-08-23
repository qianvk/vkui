// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMargins>
#include <QtCore/QPointF>
#include <QtCore/QRect>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>
#include <QtCore/Qt>
#include <vkui/widgets/overlays/VPopover.h>

namespace vkui {

/** Input for the deterministic, QWidget-independent placement calculation. */
struct VPopoverPlacementInput final {
    QRectF anchorRect;
    QSizeF contentSize;
    VPopoverPlacement preferredPlacement = VPopoverPlacement::Automatic;
    VPopoverCrossAxisAlignment crossAxisAlignment = VPopoverCrossAxisAlignment::Center;
    QRectF availableGeometry;
    QRectF boundaryGeometry;
    VPopoverBoundaryPlacements boundaryPlacements = VPopoverBoundaryPlacementFlag::None;
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
struct VPopoverPlacementResult final {
    VPopoverPlacement resolvedPlacement = VPopoverPlacement::Automatic;
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

/** Pure placement policy for VPopover. */
class VPopoverPlacementEngine final {
  public:
    [[nodiscard]] static VPopoverPlacementResult calculate(const VPopoverPlacementInput& input);

    [[nodiscard]] static VPopoverPlacementResult place(const VPopoverPlacementInput& input) {
        return calculate(input);
    }
};

} // namespace vkui
