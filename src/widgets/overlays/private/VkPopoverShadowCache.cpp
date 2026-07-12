// SPDX-License-Identifier: MIT

#include "VkPopoverShadowCache_p.h"

#include <QtCore/QtMath>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <algorithm>
#include <cmath>
#include <vector>

namespace vkui {
namespace {

void boxBlur(QImage& image, int radius, std::vector<QRgb>& line) {
    if (radius <= 0 || image.isNull()) {
        return;
    }

    const int width = image.width();
    const int height = image.height();
    const int divisor = 2 * radius + 1;
    line.resize(static_cast<std::size_t>(std::max(width, height)));

    for (int y = 0; y < height; ++y) {
        const auto* source = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        std::copy_n(source, width, line.begin());
        auto* destination = reinterpret_cast<QRgb*>(image.scanLine(y));
        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 0;
        for (int sample = -radius; sample <= radius; ++sample) {
            if (sample >= 0 && sample < width) {
                const QRgb pixel = line[static_cast<std::size_t>(sample)];
                red += qRed(pixel);
                green += qGreen(pixel);
                blue += qBlue(pixel);
                alpha += qAlpha(pixel);
            }
        }
        for (int x = 0; x < width; ++x) {
            destination[x] = qRgba(red / divisor, green / divisor, blue / divisor, alpha / divisor);
            const int leaving = x - radius;
            const int entering = x + radius + 1;
            if (leaving >= 0) {
                const QRgb pixel = line[static_cast<std::size_t>(leaving)];
                red -= qRed(pixel);
                green -= qGreen(pixel);
                blue -= qBlue(pixel);
                alpha -= qAlpha(pixel);
            }
            if (entering < width) {
                const QRgb pixel = line[static_cast<std::size_t>(entering)];
                red += qRed(pixel);
                green += qGreen(pixel);
                blue += qBlue(pixel);
                alpha += qAlpha(pixel);
            }
        }
    }

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            const auto* source = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            line[static_cast<std::size_t>(y)] = source[x];
        }
        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 0;
        for (int sample = -radius; sample <= radius; ++sample) {
            if (sample >= 0 && sample < height) {
                const QRgb pixel = line[static_cast<std::size_t>(sample)];
                red += qRed(pixel);
                green += qGreen(pixel);
                blue += qBlue(pixel);
                alpha += qAlpha(pixel);
            }
        }
        for (int y = 0; y < height; ++y) {
            auto* destination = reinterpret_cast<QRgb*>(image.scanLine(y));
            destination[x] = qRgba(red / divisor, green / divisor, blue / divisor, alpha / divisor);
            const int leaving = y - radius;
            const int entering = y + radius + 1;
            if (leaving >= 0) {
                const QRgb pixel = line[static_cast<std::size_t>(leaving)];
                red -= qRed(pixel);
                green -= qGreen(pixel);
                blue -= qBlue(pixel);
                alpha -= qAlpha(pixel);
            }
            if (entering < height) {
                const QRgb pixel = line[static_cast<std::size_t>(entering)];
                red += qRed(pixel);
                green += qGreen(pixel);
                blue += qBlue(pixel);
                alpha += qAlpha(pixel);
            }
        }
    }
}

} // namespace

const QPixmap& VkPopoverShadowCache::shadow(const QPainterPath& path, const QSize& logicalSize,
                                            qreal devicePixelRatio, const QColor& color,
                                            qreal blurRadius, const QPointF& offset) {
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    const qreal radius = std::max<qreal>(0.0, blurRadius);
    if (!pixmap_.isNull() && path_ == path && logicalSize_ == logicalSize &&
        qFuzzyCompare(devicePixelRatio_, dpr) && color_ == color &&
        qFuzzyCompare(blurRadius_ + 1.0, radius + 1.0) && offset_ == offset) {
        return pixmap_;
    }

    path_ = path;
    logicalSize_ = logicalSize;
    devicePixelRatio_ = dpr;
    color_ = color;
    blurRadius_ = radius;
    offset_ = offset;

    if (logicalSize.isEmpty() || path.isEmpty() || color.alpha() == 0) {
        pixmap_ = {};
        return pixmap_;
    }

    const QSize pixelSize(std::max(1, qCeil(logicalSize.width() * dpr)),
                          std::max(1, qCeil(logicalSize.height() * dpr)));
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(offset);
        painter.fillPath(path, color);
    }

    // Two compact box passes approximate a Gaussian while bounding memory and
    // work. A scanline scratch buffer replaces a second full-size image, and
    // the result is reused until geometry, DPR, or theme changes.
    if (radius > 0.0) {
        const int passRadius = std::max(1, qCeil(radius * dpr * 0.42));
        std::vector<QRgb> line;
        boxBlur(image, passRadius, line);
        boxBlur(image, passRadius, line);
    }
    pixmap_ = QPixmap::fromImage(std::move(image));
    pixmap_.setDevicePixelRatio(dpr);
    return pixmap_;
}

void VkPopoverShadowCache::invalidate() {
    path_ = {};
    logicalSize_ = {};
    pixmap_ = {};
    devicePixelRatio_ = 0.0;
    blurRadius_ = -1.0;
}

} // namespace vkui
