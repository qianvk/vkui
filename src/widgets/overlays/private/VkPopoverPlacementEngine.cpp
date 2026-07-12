// SPDX-License-Identifier: MIT

#include "VkPopoverPlacementEngine_p.h"

#include <QtCore/QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace vkui {
namespace {

constexpr qreal kGeometryEpsilon = 0.001;

qreal nonNegative(qreal value) noexcept {
    return std::isfinite(value) ? std::max<qreal>(0.0, value) : 0.0;
}

qreal clamped(qreal value, qreal minimum, qreal maximum) noexcept {
    if (maximum < minimum) {
        return minimum;
    }
    return std::clamp(value, minimum, maximum);
}

qreal area(const QRectF& rect) noexcept {
    return rect.isEmpty() ? 0.0 : rect.width() * rect.height();
}

bool isFinite(const QRectF& rect) noexcept {
    return std::isfinite(rect.x()) && std::isfinite(rect.y()) && std::isfinite(rect.width()) &&
           std::isfinite(rect.height());
}

bool containsRect(const QRectF& outer, const QRectF& inner) noexcept {
    return inner.left() >= outer.left() - kGeometryEpsilon &&
           inner.top() >= outer.top() - kGeometryEpsilon &&
           inner.right() <= outer.right() + kGeometryEpsilon &&
           inner.bottom() <= outer.bottom() + kGeometryEpsilon;
}

struct Candidate final {
    VkPopoverPlacementResult result;
    bool fullyFits = false;
    qreal visibleArea = 0.0;
    qreal displacement = std::numeric_limits<qreal>::max();
    int priority = 0;
};

QRectF contentRectFor(const QRectF& body, const QMarginsF& margins) {
    QRectF content =
        body.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
    if (content.width() < 0.0) {
        content.setLeft(content.center().x());
        content.setRight(content.left());
    }
    if (content.height() < 0.0) {
        content.setTop(content.center().y());
        content.setBottom(content.top());
    }
    return content;
}

std::vector<VkPopoverPlacement> placementOrder(const VkPopoverPlacementInput& input) {
    if (input.preferredPlacement != VkPopoverPlacement::Automatic) {
        return {input.preferredPlacement};
    }

    if (input.layoutDirection == Qt::RightToLeft) {
        return {VkPopoverPlacement::Below, VkPopoverPlacement::Above, VkPopoverPlacement::Left,
                VkPopoverPlacement::Right};
    }
    return {VkPopoverPlacement::Below, VkPopoverPlacement::Above, VkPopoverPlacement::Right,
            VkPopoverPlacement::Left};
}

Candidate makeCandidate(const VkPopoverPlacementInput& input, const QRectF& anchor,
                        const QRectF& usable, VkPopoverPlacement placement, int priority) {
    Candidate candidate;
    candidate.priority = priority;

    const qreal outer = nonNegative(input.outerMargin);
    const qreal gap = nonNegative(input.anchorGap);
    const qreal arrowDepth = nonNegative(input.arrowDepth);
    const qreal arrowWidth = nonNegative(input.arrowWidth);
    const qreal radius = nonNegative(input.bodyCornerRadius);
    const QMarginsF margins(
        nonNegative(input.contentMargins.left()), nonNegative(input.contentMargins.top()),
        nonNegative(input.contentMargins.right()), nonNegative(input.contentMargins.bottom()));

    const bool vertical =
        placement == VkPopoverPlacement::Below || placement == VkPopoverPlacement::Above;
    const qreal minimumArrowEdge = 2.0 * (radius + arrowWidth / 2.0 + 1.0);
    const qreal minimumPlainEdge = std::max<qreal>(1.0, 2.0 * radius + 1.0);

    qreal requestedBodyWidth =
        nonNegative(input.contentSize.width()) + margins.left() + margins.right();
    qreal requestedBodyHeight =
        nonNegative(input.contentSize.height()) + margins.top() + margins.bottom();
    requestedBodyWidth =
        std::max(requestedBodyWidth, vertical ? minimumArrowEdge : minimumPlainEdge);
    requestedBodyHeight =
        std::max(requestedBodyHeight, vertical ? minimumPlainEdge : minimumArrowEdge);

    const qreal requestedWidth = requestedBodyWidth + 2.0 * outer + (vertical ? 0.0 : arrowDepth);
    const qreal requestedHeight = requestedBodyHeight + 2.0 * outer + (vertical ? arrowDepth : 0.0);

    const qreal targetX = anchor.center().x();
    const qreal targetY = anchor.center().y();
    QRectF desired;
    switch (placement) {
    case VkPopoverPlacement::Below: {
        const qreal tipY = anchor.bottom() + gap;
        desired =
            QRectF(targetX - requestedWidth / 2.0, tipY - outer, requestedWidth, requestedHeight);
        break;
    }
    case VkPopoverPlacement::Above: {
        const qreal tipY = anchor.top() - gap;
        desired = QRectF(targetX - requestedWidth / 2.0, tipY - (requestedHeight - outer),
                         requestedWidth, requestedHeight);
        break;
    }
    case VkPopoverPlacement::Right: {
        const qreal tipX = anchor.right() + gap;
        desired =
            QRectF(tipX - outer, targetY - requestedHeight / 2.0, requestedWidth, requestedHeight);
        break;
    }
    case VkPopoverPlacement::Left: {
        const qreal tipX = anchor.left() - gap;
        desired = QRectF(tipX - (requestedWidth - outer), targetY - requestedHeight / 2.0,
                         requestedWidth, requestedHeight);
        break;
    }
    case VkPopoverPlacement::Automatic:
        return candidate;
    }

    candidate.fullyFits = containsRect(usable, desired);
    candidate.visibleArea = area(desired.intersected(usable));

    qreal bodyWidth = requestedBodyWidth;
    qreal bodyHeight = requestedBodyHeight;
    qreal maximumBodyWidth = usable.width() - 2.0 * outer;
    qreal maximumBodyHeight = usable.height() - 2.0 * outer;
    qreal resolvedTipX = targetX;
    qreal resolvedTipY = targetY;

    // The trigger may legitimately sit between the physical screen edge and
    // the configured screen margin. Move the transparent outer edge inward in
    // that case instead of rejecting the direction; the arrow remains aimed
    // at the trigger while its body and shadow stay inside usable geometry.
    switch (placement) {
    case VkPopoverPlacement::Below:
        resolvedTipY = std::max(anchor.bottom() + gap, usable.top() + outer);
        maximumBodyHeight = usable.bottom() - resolvedTipY - arrowDepth - outer;
        break;
    case VkPopoverPlacement::Above:
        resolvedTipY = std::min(anchor.top() - gap, usable.bottom() - outer);
        maximumBodyHeight = resolvedTipY - usable.top() - arrowDepth - outer;
        break;
    case VkPopoverPlacement::Right:
        resolvedTipX = std::max(anchor.right() + gap, usable.left() + outer);
        maximumBodyWidth = usable.right() - resolvedTipX - arrowDepth - outer;
        break;
    case VkPopoverPlacement::Left:
        resolvedTipX = std::min(anchor.left() - gap, usable.right() - outer);
        maximumBodyWidth = resolvedTipX - usable.left() - arrowDepth - outer;
        break;
    case VkPopoverPlacement::Automatic:
        return candidate;
    }

    if (maximumBodyWidth + kGeometryEpsilon < (vertical ? minimumArrowEdge : minimumPlainEdge) ||
        maximumBodyHeight + kGeometryEpsilon < (vertical ? minimumPlainEdge : minimumArrowEdge)) {
        return candidate;
    }

    bodyWidth = std::min(bodyWidth, std::max<qreal>(0.0, maximumBodyWidth));
    bodyHeight = std::min(bodyHeight, std::max<qreal>(0.0, maximumBodyHeight));

    const qreal totalWidth = bodyWidth + 2.0 * outer + (vertical ? 0.0 : arrowDepth);
    const qreal totalHeight = bodyHeight + 2.0 * outer + (vertical ? arrowDepth : 0.0);
    const int popupWidth = std::max(1, qCeil(totalWidth));
    const int popupHeight = std::max(1, qCeil(totalHeight));

    qreal popupX = 0.0;
    qreal popupY = 0.0;
    switch (placement) {
    case VkPopoverPlacement::Below:
        popupX = clamped(targetX - totalWidth / 2.0, usable.left(), usable.right() - popupWidth);
        popupY = resolvedTipY - outer;
        break;
    case VkPopoverPlacement::Above:
        popupX = clamped(targetX - totalWidth / 2.0, usable.left(), usable.right() - popupWidth);
        popupY = resolvedTipY - (totalHeight - outer);
        break;
    case VkPopoverPlacement::Right:
        popupX = resolvedTipX - outer;
        popupY = clamped(targetY - totalHeight / 2.0, usable.top(), usable.bottom() - popupHeight);
        break;
    case VkPopoverPlacement::Left:
        popupX = resolvedTipX - (totalWidth - outer);
        popupY = clamped(targetY - totalHeight / 2.0, usable.top(), usable.bottom() - popupHeight);
        break;
    case VkPopoverPlacement::Automatic:
        return candidate;
    }

    int popupLeft = qRound(popupX);
    int popupTop = qRound(popupY);
    popupLeft = std::clamp(popupLeft, qCeil(usable.left()), qFloor(usable.right()) - popupWidth);
    popupTop = std::clamp(popupTop, qCeil(usable.top()), qFloor(usable.bottom()) - popupHeight);
    const QRect popupRect(popupLeft, popupTop, popupWidth, popupHeight);

    QRectF bodyRect;
    switch (placement) {
    case VkPopoverPlacement::Below:
        bodyRect = QRectF(outer, outer + arrowDepth, bodyWidth, bodyHeight);
        break;
    case VkPopoverPlacement::Above:
        bodyRect = QRectF(outer, outer, bodyWidth, bodyHeight);
        break;
    case VkPopoverPlacement::Right:
        bodyRect = QRectF(outer + arrowDepth, outer, bodyWidth, bodyHeight);
        break;
    case VkPopoverPlacement::Left:
        bodyRect = QRectF(outer, outer, bodyWidth, bodyHeight);
        break;
    case VkPopoverPlacement::Automatic:
        return candidate;
    }

    const qreal effectiveRadius =
        std::min({radius, bodyRect.width() / 2.0, bodyRect.height() / 2.0});
    const qreal clearance = effectiveRadius + arrowWidth / 2.0;
    QPointF arrowBase;
    QPointF arrowTip;

    if (vertical) {
        const qreal minimumBase = popupRect.left() + bodyRect.left() + clearance;
        const qreal maximumBase = popupRect.left() + bodyRect.right() - clearance;
        if (maximumBase < minimumBase - kGeometryEpsilon) {
            return candidate;
        }
        const qreal baseGlobal = clamped(anchor.center().x(), minimumBase, maximumBase);
        const qreal anchorTarget = clamped(baseGlobal, anchor.left(), anchor.right());
        // Rounded corners constrain the base, not the tip. Letting the tip
        // reach the closest point on the anchor preserves identification near
        // a screen corner even when this produces an intentionally skewed
        // curved arrow.
        const qreal tipGlobal =
            clamped(anchorTarget, popupRect.left(), popupRect.left() + popupRect.width());
        const qreal baseY =
            placement == VkPopoverPlacement::Below ? bodyRect.top() : bodyRect.bottom();
        const qreal tipY =
            placement == VkPopoverPlacement::Below ? outer : bodyRect.bottom() + arrowDepth;
        arrowBase = QPointF(baseGlobal - popupRect.left(), baseY);
        arrowTip = QPointF(tipGlobal - popupRect.left(), tipY);
    } else {
        const qreal minimumBase = popupRect.top() + bodyRect.top() + clearance;
        const qreal maximumBase = popupRect.top() + bodyRect.bottom() - clearance;
        if (maximumBase < minimumBase - kGeometryEpsilon) {
            return candidate;
        }
        const qreal baseGlobal = clamped(anchor.center().y(), minimumBase, maximumBase);
        const qreal anchorTarget = clamped(baseGlobal, anchor.top(), anchor.bottom());
        const qreal tipGlobal =
            clamped(anchorTarget, popupRect.top(), popupRect.top() + popupRect.height());
        const qreal baseX =
            placement == VkPopoverPlacement::Right ? bodyRect.left() : bodyRect.right();
        const qreal tipX =
            placement == VkPopoverPlacement::Right ? outer : bodyRect.right() + arrowDepth;
        arrowBase = QPointF(baseX, baseGlobal - popupRect.top());
        arrowTip = QPointF(tipX, tipGlobal - popupRect.top());
    }

    candidate.displacement =
        std::abs(popupLeft - desired.left()) + std::abs(popupTop - desired.top()) +
        std::abs(requestedBodyWidth - bodyWidth) + std::abs(requestedBodyHeight - bodyHeight);
    candidate.result.resolvedPlacement = placement;
    candidate.result.popupRect = popupRect;
    candidate.result.bodyRect = bodyRect;
    candidate.result.contentRect = contentRectFor(bodyRect, margins);
    candidate.result.arrowTip = arrowTip;
    candidate.result.arrowBaseCenter = arrowBase;
    candidate.result.valid = true;
    return candidate;
}

bool betterFallback(const Candidate& candidate, const Candidate& best) noexcept {
    if (candidate.visibleArea > best.visibleArea + kGeometryEpsilon) {
        return true;
    }
    if (std::abs(candidate.visibleArea - best.visibleArea) <= kGeometryEpsilon) {
        if (candidate.displacement < best.displacement - kGeometryEpsilon) {
            return true;
        }
        if (std::abs(candidate.displacement - best.displacement) <= kGeometryEpsilon) {
            return candidate.priority < best.priority;
        }
    }
    return false;
}

} // namespace

VkPopoverPlacementResult VkPopoverPlacementEngine::calculate(const VkPopoverPlacementInput& input) {
    VkPopoverPlacementResult invalid;
    if (!isFinite(input.anchorRect) || !isFinite(input.availableGeometry) ||
        !input.availableGeometry.isValid() || input.availableGeometry.isEmpty() ||
        nonNegative(input.arrowWidth) <= kGeometryEpsilon ||
        nonNegative(input.arrowDepth) <= kGeometryEpsilon) {
        return invalid;
    }

    QRectF anchor = input.anchorRect.normalized();
    const qreal margin = nonNegative(input.screenMargin);
    const QRectF available = input.availableGeometry.normalized();
    const qreal usableLeft = std::ceil(available.left() + margin);
    const qreal usableTop = std::ceil(available.top() + margin);
    const qreal usableRight = std::floor(available.right() - margin);
    const qreal usableBottom = std::floor(available.bottom() - margin);
    const QRectF usable(usableLeft, usableTop, usableRight - usableLeft, usableBottom - usableTop);
    if (usable.width() < 1.0 || usable.height() < 1.0) {
        return invalid;
    }

    const auto order = placementOrder(input);
    std::vector<Candidate> candidates;
    candidates.reserve(order.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        Candidate candidate =
            makeCandidate(input, anchor, usable, order[index], static_cast<int>(index));
        if (candidate.result.valid) {
            candidates.push_back(std::move(candidate));
        }
    }
    if (candidates.empty()) {
        return invalid;
    }

    if (input.preferredPlacement == VkPopoverPlacement::Automatic) {
        for (const Candidate& candidate : candidates) {
            if (candidate.fullyFits) {
                return candidate.result;
            }
        }
    }

    const Candidate* best = &candidates.front();
    for (const Candidate& candidate : candidates) {
        if (betterFallback(candidate, *best)) {
            best = &candidate;
        }
    }
    return best->result;
}

} // namespace vkui
