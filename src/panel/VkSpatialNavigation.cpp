#include <vkui/panel/VkSpatialNavigation.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vkui::panel {
namespace {

[[nodiscard]] qreal intervalGap(
    const qreal firstStart,
    const qreal firstEnd,
    const qreal secondStart,
    const qreal secondEnd) noexcept
{
    if (secondEnd < firstStart) {
        return firstStart - secondEnd;
    }
    if (secondStart > firstEnd) {
        return secondStart - firstEnd;
    }
    return 0.0;
}

[[nodiscard]] qreal intervalOverlap(
    const qreal firstStart,
    const qreal firstEnd,
    const qreal secondStart,
    const qreal secondEnd) noexcept
{
    return std::max<qreal>(
        0.0,
        std::min(firstEnd, secondEnd)
            - std::max(firstStart, secondStart));
}

} // namespace

std::optional<VkSpatialNavigationRank> spatialNavigationRank(
    const QRectF &originGeometry,
    const QRectF &targetGeometry,
    const int horizontalDirection,
    const int verticalDirection)
{
    const int horizontal =
        std::clamp(horizontalDirection, -1, 1);
    const int vertical =
        std::clamp(verticalDirection, -1, 1);
    if (!originGeometry.isValid()
        || !targetGeometry.isValid()
        || (horizontal == 0 && vertical == 0)) {
        return std::nullopt;
    }

    const QRectF source = originGeometry.normalized();
    const QPointF sourceCenter = source.center();
    const QRectF target = targetGeometry.normalized();
    const QPointF center = target.center();
    const qreal dx = center.x() - sourceCenter.x();
    const qreal dy = center.y() - sourceCenter.y();
    if ((horizontal < 0 && dx >= 0.0)
        || (horizontal > 0 && dx <= 0.0)
        || (vertical < 0 && dy >= 0.0)
        || (vertical > 0 && dy <= 0.0)) {
        return std::nullopt;
    }

    qreal forwardGap = 0.0;
    qreal perpendicularGap = 0.0;
    qreal overlap = 0.0;
    qreal centerOffset = 0.0;
    qreal forwardCenterDistance = 0.0;
    if (horizontal != 0 && vertical == 0) {
        forwardGap = horizontal > 0
            ? std::max<qreal>(0.0, target.left() - source.right())
            : std::max<qreal>(0.0, source.left() - target.right());
        perpendicularGap = intervalGap(
            source.top(), source.bottom(),
            target.top(), target.bottom());
        overlap = intervalOverlap(
            source.top(), source.bottom(),
            target.top(), target.bottom());
        centerOffset = std::abs(dy);
        forwardCenterDistance = std::abs(dx);
    } else if (vertical != 0 && horizontal == 0) {
        forwardGap = vertical > 0
            ? std::max<qreal>(0.0, target.top() - source.bottom())
            : std::max<qreal>(0.0, source.top() - target.bottom());
        perpendicularGap = intervalGap(
            source.left(), source.right(),
            target.left(), target.right());
        overlap = intervalOverlap(
            source.left(), source.right(),
            target.left(), target.right());
        centerOffset = std::abs(dx);
        forwardCenterDistance = std::abs(dy);
    } else {
        const qreal horizontalGap = horizontal > 0
            ? std::max<qreal>(0.0, target.left() - source.right())
            : std::max<qreal>(0.0, source.left() - target.right());
        const qreal verticalGap = vertical > 0
            ? std::max<qreal>(0.0, target.top() - source.bottom())
            : std::max<qreal>(0.0, source.top() - target.bottom());
        forwardGap = std::hypot(horizontalGap, verticalGap);
        const qreal dot = std::abs(dx) + std::abs(dy);
        perpendicularGap = dot <= 0.0
            ? std::numeric_limits<qreal>::max()
            : std::abs(std::abs(dx) - std::abs(dy)) / dot;
        overlap = 0.0;
        centerOffset = std::hypot(dx, dy);
        forwardCenterDistance = centerOffset;
    }

    return VkSpatialNavigationRank{
        perpendicularGap > 0.0 ? 1 : 0,
        forwardGap,
        perpendicularGap,
        -overlap,
        centerOffset + forwardCenterDistance * 1.0e-6,
        horizontal != 0 ? target.top() : target.left(),
        horizontal != 0 ? target.left() : target.top()};
}

std::optional<vkui::vk::WindowId> nearestSpatialWindow(
    const vkui::vk::WindowId origin,
    const QRectF &originGeometry,
    const QVector<VkSpatialWindow> &candidates,
    const int horizontalDirection,
    const int verticalDirection)
{
    std::optional<VkSpatialNavigationRank> best;
    std::optional<vkui::vk::WindowId> result;
    for (const VkSpatialWindow &candidate : candidates) {
        if (candidate.window == 0
            || candidate.window == origin) {
            continue;
        }
        const auto rank = spatialNavigationRank(
            originGeometry,
            candidate.geometry,
            horizontalDirection,
            verticalDirection);
        if (!rank) {
            continue;
        }

        const bool equalRank = best
            && !(*rank < *best)
            && !(*best < *rank);
        if (!best || *rank < *best
            || (equalRank
                && (!result
                    || candidate.window < *result))) {
            best = *rank;
            result = candidate.window;
        }
    }
    return result;
}

} // namespace vkui::panel
