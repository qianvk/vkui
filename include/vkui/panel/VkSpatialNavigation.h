#pragma once

#include <vkui/vk/VkTypes.h>

#include <QRectF>
#include <QVector>

#include <optional>
#include <tuple>

namespace vkui::panel {

/** One visible window participating in geometric focus navigation. */
struct VkSpatialWindow final
{
    vkui::vk::WindowId window = 0;
    QRectF geometry;
};

/** Lexicographic geometric rank shared by model and widget projections. */
struct VkSpatialNavigationRank final
{
    int projectionClass = 1;
    qreal forwardGap = 0.0;
    qreal perpendicularGap = 0.0;
    qreal negativeOverlap = 0.0;
    qreal centerDistance = 0.0;
    qreal screenOrderPrimary = 0.0;
    qreal screenOrderSecondary = 0.0;

    friend bool operator<(
        const VkSpatialNavigationRank &left,
        const VkSpatialNavigationRank &right) noexcept
    {
        return std::tie(
                   left.projectionClass,
                   left.forwardGap,
                   left.perpendicularGap,
                   left.negativeOverlap,
                   left.centerDistance,
                   left.screenOrderPrimary,
                   left.screenOrderSecondary)
            < std::tie(
                   right.projectionClass,
                   right.forwardGap,
                   right.perpendicularGap,
                   right.negativeOverlap,
                   right.centerDistance,
                   right.screenOrderPrimary,
                   right.screenOrderSecondary);
    }
};

/** Returns no rank when target is outside the requested directional cone. */
[[nodiscard]] std::optional<VkSpatialNavigationRank>
spatialNavigationRank(
    const QRectF &originGeometry,
    const QRectF &targetGeometry,
    int horizontalDirection,
    int verticalDirection = 0);

/**
 * Finds the first unobstructed rectangle in one screen direction.
 *
 * Axis-aligned navigation is ordered lexicographically: candidates whose
 * perpendicular projection intersects the origin come first, followed by
 * the closest facing edge, the remaining interval gap and center offset.
 * Unlike a weighted center-distance score, a far, aligned rectangle can
 * therefore never jump across a nearer panel that spans the travel ray.
 */
[[nodiscard]] std::optional<vkui::vk::WindowId>
nearestSpatialWindow(
    vkui::vk::WindowId origin,
    const QRectF &originGeometry,
    const QVector<VkSpatialWindow> &candidates,
    int horizontalDirection,
    int verticalDirection = 0);

} // namespace vkui::panel
