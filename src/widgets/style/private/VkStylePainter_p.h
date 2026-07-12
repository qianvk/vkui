// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QPainterPath>
#include <QRectF>
#include <Qt>

class QPainter;

namespace vkui {

class VkStylePainter final {
  public:
    static QColor mix(const QColor& from, const QColor& to, qreal progress);
    static QColor multiplyAlpha(const QColor& color, qreal factor);
    static QColor contrastingText(const QColor& background);
    static QColor neutralFocusColor(const QColor& borderColor);
    static bool showsKeyboardFocus(Qt::FocusReason reason) noexcept;

    static QPainterPath roundedRectPath(const QRectF& rect, qreal radius);
    static void drawRoundedPanel(QPainter& painter, const QRectF& rect, qreal radius,
                                 const QColor& fill, const QColor& border, qreal borderWidth);
    static void drawFocusRing(QPainter& painter, const QRectF& rect, qreal radius,
                              const QColor& color, qreal width);
    static void drawHairline(QPainter& painter, const QPointF& start, const QPointF& end,
                             const QColor& color);
    static void drawCheckmark(QPainter& painter, const QRectF& rect, const QColor& color,
                              qreal width);
    static void drawChevron(QPainter& painter, const QRectF& rect, Qt::ArrowType direction,
                            const QColor& color, qreal width);
};

} // namespace vkui
