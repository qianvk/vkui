// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtGui/QPainterPath>
#include <vkui/widgets/overlays/VPopover.h>

namespace vkui {

struct VPopoverPathInput final {
    QRectF bodyRect;
    VPopoverPlacement placement = VPopoverPlacement::Below;
    QPointF arrowTip;
    QPointF arrowBaseCenter;
    qreal arrowWidth = 0.0;
    qreal cornerRadius = 0.0;
};

/** Builds the single continuous outline used by fill, border, and shadow. */
class VPopoverPath final {
  public:
    [[nodiscard]] static QPainterPath create(const VPopoverPathInput& input);

    [[nodiscard]] static QPainterPath create(const QRectF& bodyRect, VPopoverPlacement placement,
                                             const QPointF& arrowTip,
                                             const QPointF& arrowBaseCenter, qreal arrowWidth,
                                             qreal cornerRadius) {
        return create({bodyRect, placement, arrowTip, arrowBaseCenter, arrowWidth, cornerRadius});
    }
};

} // namespace vkui
