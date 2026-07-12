// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QPointF>
#include <QtCore/QSize>
#include <QtCore/QtTypes>
#include <QtGui/QColor>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>

namespace vkui {

/**
 * A one-entry, DPR-aware cache for the popover's final-geometry shadow.
 * Animation transforms the cached pixmap instead of regenerating its blur.
 */
class VkPopoverShadowCache final {
  public:
    [[nodiscard]] const QPixmap& shadow(const QPainterPath& path, const QSize& logicalSize,
                                        qreal devicePixelRatio, const QColor& color,
                                        qreal blurRadius, const QPointF& offset);
    void invalidate();

  private:
    QPainterPath path_;
    QSize logicalSize_;
    qreal devicePixelRatio_ = 0.0;
    QColor color_;
    qreal blurRadius_ = -1.0;
    QPointF offset_;
    QPixmap pixmap_;
};

} // namespace vkui
