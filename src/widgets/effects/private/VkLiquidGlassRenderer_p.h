// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QSize>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <vkui/widgets/effects/VLiquidGlass.h>

namespace vkui {

struct VkLiquidGlassFrame final {
    QImage image;
    int padding = 0;
    qreal sampleScale = 1.0;
};

class VkLiquidGlassRenderer final {
  public:
    [[nodiscard]] static int capturePadding(const VLiquidGlassStyle& style) noexcept;
    [[nodiscard]] static qreal sampleScale(VLiquidGlassQuality quality,
                                           qreal devicePixelRatio) noexcept;
    [[nodiscard]] static QImage render(const VkLiquidGlassFrame& frame, const QSize& logicalSize,
                                       const VLiquidGlassStyle& style, const QColor& tint);
};

} // namespace vkui
