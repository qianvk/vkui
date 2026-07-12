// SPDX-License-Identifier: MIT

#include "VkStylePainter_p.h"

#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>

namespace vkui {

QColor VkStylePainter::mix(const QColor& from, const QColor& to, qreal progress) {
    progress = std::clamp(progress, 0.0, 1.0);
    const auto interpolate = [progress](float fromChannel, float toChannel) {
        return static_cast<float>(fromChannel + (toChannel - fromChannel) * progress);
    };
    return QColor::fromRgbF(
        interpolate(from.redF(), to.redF()), interpolate(from.greenF(), to.greenF()),
        interpolate(from.blueF(), to.blueF()), interpolate(from.alphaF(), to.alphaF()));
}

QColor VkStylePainter::multiplyAlpha(const QColor& color, qreal factor) {
    QColor result = color;
    result.setAlphaF(static_cast<float>(std::clamp(color.alphaF() * factor, 0.0, 1.0)));
    return result;
}

QColor VkStylePainter::contrastingText(const QColor& background) {
    const auto linearChannel = [](qreal channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    const qreal luminance = linearChannel(background.redF()) * 0.2126 +
                            linearChannel(background.greenF()) * 0.7152 +
                            linearChannel(background.blueF()) * 0.0722;
    const qreal blackContrast = (luminance + 0.05) / 0.05;
    const qreal whiteContrast = 1.05 / (luminance + 0.05);
    return blackContrast >= whiteContrast ? QColor(Qt::black) : QColor(Qt::white);
}

QColor VkStylePainter::neutralFocusColor(const QColor& borderColor) {
    return multiplyAlpha(borderColor, 0.72);
}

bool VkStylePainter::showsKeyboardFocus(Qt::FocusReason reason) noexcept {
    // Window activation alone should not reveal a ring. OtherFocusReason is
    // retained for programmatic and assistive focus requests.
    return reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
           reason == Qt::ShortcutFocusReason || reason == Qt::MenuBarFocusReason ||
           reason == Qt::OtherFocusReason;
}

QPainterPath VkStylePainter::roundedRectPath(const QRectF& rect, qreal radius) {
    QPainterPath path;
    path.addRoundedRect(rect, std::max<qreal>(0.0, radius), std::max<qreal>(0.0, radius));
    return path;
}

void VkStylePainter::drawRoundedPanel(QPainter& painter, const QRectF& rect, qreal radius,
                                      const QColor& fill, const QColor& border, qreal borderWidth) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal inset = std::max<qreal>(0.0, borderWidth) * 0.5;
    const QRectF aligned = rect.adjusted(inset, inset, -inset, -inset);
    const QPainterPath path = roundedRectPath(aligned, std::max<qreal>(0.0, radius - inset));
    painter.fillPath(path, fill);
    if (borderWidth > 0.0 && border.alpha() > 0) {
        QPen pen(border, borderWidth);
        pen.setCosmetic(false);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    painter.restore();
}

void VkStylePainter::drawFocusRing(QPainter& painter, const QRectF& rect, qreal radius,
                                   const QColor& color, qreal width) {
    if (width <= 0.0 || color.alpha() == 0) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, width);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const qreal inset = width * 0.5;
    painter.drawRoundedRect(rect.adjusted(inset, inset, -inset, -inset),
                            std::max<qreal>(0.0, radius - inset),
                            std::max<qreal>(0.0, radius - inset));
    painter.restore();
}

void VkStylePainter::drawHairline(QPainter& painter, const QPointF& start, const QPointF& end,
                                  const QColor& color) {
    painter.save();
    QPen pen(color, 0.0);
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);
    painter.drawLine(start, end);
    painter.restore();
}

void VkStylePainter::drawCheckmark(QPainter& painter, const QRectF& rect, const QColor& color,
                                   qreal width) {
    QPainterPath path;
    path.moveTo(rect.left() + rect.width() * 0.20, rect.top() + rect.height() * 0.52);
    path.lineTo(rect.left() + rect.width() * 0.43, rect.top() + rect.height() * 0.74);
    path.lineTo(rect.left() + rect.width() * 0.82, rect.top() + rect.height() * 0.28);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

void VkStylePainter::drawChevron(QPainter& painter, const QRectF& rect, Qt::ArrowType direction,
                                 const QColor& color, qreal width) {
    const QPointF center = rect.center();
    const qreal extent = std::min(rect.width(), rect.height()) * 0.26;
    QPainterPath path;
    switch (direction) {
    case Qt::LeftArrow:
        path.moveTo(center.x() + extent * 0.5, center.y() - extent);
        path.lineTo(center.x() - extent * 0.5, center.y());
        path.lineTo(center.x() + extent * 0.5, center.y() + extent);
        break;
    case Qt::RightArrow:
        path.moveTo(center.x() - extent * 0.5, center.y() - extent);
        path.lineTo(center.x() + extent * 0.5, center.y());
        path.lineTo(center.x() - extent * 0.5, center.y() + extent);
        break;
    case Qt::UpArrow:
        path.moveTo(center.x() - extent, center.y() + extent * 0.5);
        path.lineTo(center.x(), center.y() - extent * 0.5);
        path.lineTo(center.x() + extent, center.y() + extent * 0.5);
        break;
    case Qt::DownArrow:
    default:
        path.moveTo(center.x() - extent, center.y() - extent * 0.5);
        path.lineTo(center.x(), center.y() + extent * 0.5);
        path.lineTo(center.x() + extent, center.y() - extent * 0.5);
        break;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

} // namespace vkui
