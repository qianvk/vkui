// SPDX-License-Identifier: MIT

#include "VkPopoverPath_p.h"

#include <algorithm>
#include <cmath>

namespace vkui {
namespace {

qreal finiteNonNegative(qreal value) noexcept {
    return std::isfinite(value) ? std::max<qreal>(0.0, value) : 0.0;
}

QPainterPath roundedBody(const QRectF& body, qreal radius) {
    QPainterPath path;
    path.addRoundedRect(body, radius, radius);
    return path;
}

void appendTopArrow(QPainterPath& path, const QRectF& body, const QPointF& tip, qreal baseCenter,
                    qreal halfWidth, qreal radius) {
    const qreal leftBase = baseCenter - halfWidth;
    const qreal rightBase = baseCenter + halfWidth;
    const qreal shoulder = halfWidth * 0.58;
    const qreal tipControl = halfWidth * 0.22;
    const qreal depthControl = std::abs(body.top() - tip.y()) * 0.20;

    path.moveTo(body.left() + radius, body.top());
    path.lineTo(leftBase, body.top());
    path.cubicTo(leftBase + shoulder, body.top(), tip.x() - tipControl, tip.y() + depthControl,
                 tip.x(), tip.y());
    path.cubicTo(tip.x() + tipControl, tip.y() + depthControl, rightBase - shoulder, body.top(),
                 rightBase, body.top());
    path.lineTo(body.right() - radius, body.top());
    path.quadTo(body.right(), body.top(), body.right(), body.top() + radius);
    path.lineTo(body.right(), body.bottom() - radius);
    path.quadTo(body.right(), body.bottom(), body.right() - radius, body.bottom());
    path.lineTo(body.left() + radius, body.bottom());
    path.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - radius);
    path.lineTo(body.left(), body.top() + radius);
    path.quadTo(body.left(), body.top(), body.left() + radius, body.top());
    path.closeSubpath();
}

void appendBottomArrow(QPainterPath& path, const QRectF& body, const QPointF& tip, qreal baseCenter,
                       qreal halfWidth, qreal radius) {
    const qreal leftBase = baseCenter - halfWidth;
    const qreal rightBase = baseCenter + halfWidth;
    const qreal shoulder = halfWidth * 0.58;
    const qreal tipControl = halfWidth * 0.22;
    const qreal depthControl = std::abs(tip.y() - body.bottom()) * 0.20;

    path.moveTo(body.left() + radius, body.top());
    path.lineTo(body.right() - radius, body.top());
    path.quadTo(body.right(), body.top(), body.right(), body.top() + radius);
    path.lineTo(body.right(), body.bottom() - radius);
    path.quadTo(body.right(), body.bottom(), body.right() - radius, body.bottom());
    path.lineTo(rightBase, body.bottom());
    path.cubicTo(rightBase - shoulder, body.bottom(), tip.x() + tipControl, tip.y() - depthControl,
                 tip.x(), tip.y());
    path.cubicTo(tip.x() - tipControl, tip.y() - depthControl, leftBase + shoulder, body.bottom(),
                 leftBase, body.bottom());
    path.lineTo(body.left() + radius, body.bottom());
    path.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - radius);
    path.lineTo(body.left(), body.top() + radius);
    path.quadTo(body.left(), body.top(), body.left() + radius, body.top());
    path.closeSubpath();
}

void appendLeftArrow(QPainterPath& path, const QRectF& body, const QPointF& tip, qreal baseCenter,
                     qreal halfWidth, qreal radius) {
    const qreal upperBase = baseCenter - halfWidth;
    const qreal lowerBase = baseCenter + halfWidth;
    const qreal shoulder = halfWidth * 0.58;
    const qreal tipControl = halfWidth * 0.22;
    const qreal depthControl = std::abs(body.left() - tip.x()) * 0.20;

    path.moveTo(body.left() + radius, body.top());
    path.lineTo(body.right() - radius, body.top());
    path.quadTo(body.right(), body.top(), body.right(), body.top() + radius);
    path.lineTo(body.right(), body.bottom() - radius);
    path.quadTo(body.right(), body.bottom(), body.right() - radius, body.bottom());
    path.lineTo(body.left() + radius, body.bottom());
    path.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - radius);
    path.lineTo(body.left(), lowerBase);
    path.cubicTo(body.left(), lowerBase - shoulder, tip.x() + depthControl, tip.y() + tipControl,
                 tip.x(), tip.y());
    path.cubicTo(tip.x() + depthControl, tip.y() - tipControl, body.left(), upperBase + shoulder,
                 body.left(), upperBase);
    path.lineTo(body.left(), body.top() + radius);
    path.quadTo(body.left(), body.top(), body.left() + radius, body.top());
    path.closeSubpath();
}

void appendRightArrow(QPainterPath& path, const QRectF& body, const QPointF& tip, qreal baseCenter,
                      qreal halfWidth, qreal radius) {
    const qreal upperBase = baseCenter - halfWidth;
    const qreal lowerBase = baseCenter + halfWidth;
    const qreal shoulder = halfWidth * 0.58;
    const qreal tipControl = halfWidth * 0.22;
    const qreal depthControl = std::abs(tip.x() - body.right()) * 0.20;

    path.moveTo(body.left() + radius, body.top());
    path.lineTo(body.right() - radius, body.top());
    path.quadTo(body.right(), body.top(), body.right(), body.top() + radius);
    path.lineTo(body.right(), upperBase);
    path.cubicTo(body.right(), upperBase + shoulder, tip.x() - depthControl, tip.y() - tipControl,
                 tip.x(), tip.y());
    path.cubicTo(tip.x() - depthControl, tip.y() + tipControl, body.right(), lowerBase - shoulder,
                 body.right(), lowerBase);
    path.lineTo(body.right(), body.bottom() - radius);
    path.quadTo(body.right(), body.bottom(), body.right() - radius, body.bottom());
    path.lineTo(body.left() + radius, body.bottom());
    path.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - radius);
    path.lineTo(body.left(), body.top() + radius);
    path.quadTo(body.left(), body.top(), body.left() + radius, body.top());
    path.closeSubpath();
}

} // namespace

QPainterPath VkPopoverPath::create(const VkPopoverPathInput& input) {
    if (!input.bodyRect.isValid() || input.bodyRect.isEmpty()) {
        return {};
    }

    const QRectF body = input.bodyRect.normalized();
    const qreal radius =
        std::min({finiteNonNegative(input.cornerRadius), body.width() / 2.0, body.height() / 2.0});
    if (input.placement == VkPopoverPlacement::Automatic) {
        return roundedBody(body, radius);
    }

    const bool vertical = input.placement == VkPopoverPlacement::Below ||
                          input.placement == VkPopoverPlacement::Above;
    const qreal edgeLength = vertical ? body.width() : body.height();
    const qreal availableForArrow = std::max<qreal>(0.0, edgeLength - 2.0 * radius);
    const qreal halfWidth =
        std::min(finiteNonNegative(input.arrowWidth) / 2.0, availableForArrow / 2.0);
    if (halfWidth <= 0.0) {
        return roundedBody(body, radius);
    }

    QPainterPath path;
    if (vertical) {
        const qreal minimumCenter = body.left() + radius + halfWidth;
        const qreal maximumCenter = body.right() - radius - halfWidth;
        const qreal center = std::clamp(input.arrowBaseCenter.x(), minimumCenter, maximumCenter);
        if (input.placement == VkPopoverPlacement::Below) {
            appendTopArrow(path, body, input.arrowTip, center, halfWidth, radius);
        } else {
            appendBottomArrow(path, body, input.arrowTip, center, halfWidth, radius);
        }
    } else {
        const qreal minimumCenter = body.top() + radius + halfWidth;
        const qreal maximumCenter = body.bottom() - radius - halfWidth;
        const qreal center = std::clamp(input.arrowBaseCenter.y(), minimumCenter, maximumCenter);
        if (input.placement == VkPopoverPlacement::Right) {
            appendLeftArrow(path, body, input.arrowTip, center, halfWidth, radius);
        } else {
            appendRightArrow(path, body, input.arrowTip, center, halfWidth, radius);
        }
    }
    return path;
}

} // namespace vkui
