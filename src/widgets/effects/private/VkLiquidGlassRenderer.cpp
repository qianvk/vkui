// SPDX-License-Identifier: MIT

#include "VkLiquidGlassRenderer_p.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vkui {
namespace {

struct Sample final {
    qreal red = 0.0;
    qreal green = 0.0;
    qreal blue = 0.0;
    qreal alpha = 255.0;
};

qreal roundedRectDistance(const qreal x, const qreal y, const qreal halfWidth,
                          const qreal halfHeight, const qreal radius) noexcept {
    const qreal cornerX = std::abs(x) - std::max<qreal>(0.0, halfWidth - radius);
    const qreal cornerY = std::abs(y) - std::max<qreal>(0.0, halfHeight - radius);
    const qreal outside =
        std::hypot(std::max<qreal>(cornerX, 0.0), std::max<qreal>(cornerY, 0.0)) - radius;
    const qreal inside = std::min(std::max(cornerX, cornerY), 0.0);
    return outside + inside;
}

QImage horizontalBoxBlur(const QImage& source, const int radius) {
    if (radius <= 0 || source.isNull()) {
        return source;
    }
    const int width = source.width();
    const int height = source.height();
    const int diameter = radius * 2 + 1;
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);

    for (int y = 0; y < height; ++y) {
        const auto* input = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        auto* output = reinterpret_cast<QRgb*>(result.scanLine(y));
        std::array<qint64, 4> sum{};
        for (int offset = -radius; offset <= radius; ++offset) {
            const QRgb pixel = input[std::clamp(offset, 0, width - 1)];
            sum[0] += qRed(pixel);
            sum[1] += qGreen(pixel);
            sum[2] += qBlue(pixel);
            sum[3] += qAlpha(pixel);
        }
        for (int x = 0; x < width; ++x) {
            output[x] =
                qRgba(static_cast<int>(sum[0] / diameter), static_cast<int>(sum[1] / diameter),
                      static_cast<int>(sum[2] / diameter), static_cast<int>(sum[3] / diameter));
            const QRgb removed = input[std::clamp(x - radius, 0, width - 1)];
            const QRgb added = input[std::clamp(x + radius + 1, 0, width - 1)];
            sum[0] += qRed(added) - qRed(removed);
            sum[1] += qGreen(added) - qGreen(removed);
            sum[2] += qBlue(added) - qBlue(removed);
            sum[3] += qAlpha(added) - qAlpha(removed);
        }
    }
    return result;
}

QImage verticalBoxBlur(const QImage& source, const int radius) {
    if (radius <= 0 || source.isNull()) {
        return source;
    }
    const int width = source.width();
    const int height = source.height();
    const int diameter = radius * 2 + 1;
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);

    for (int x = 0; x < width; ++x) {
        std::array<qint64, 4> sum{};
        for (int offset = -radius; offset <= radius; ++offset) {
            const int sampleY = std::clamp(offset, 0, height - 1);
            const auto* row = reinterpret_cast<const QRgb*>(source.constScanLine(sampleY));
            const QRgb pixel = row[x];
            sum[0] += qRed(pixel);
            sum[1] += qGreen(pixel);
            sum[2] += qBlue(pixel);
            sum[3] += qAlpha(pixel);
        }
        for (int y = 0; y < height; ++y) {
            auto* output = reinterpret_cast<QRgb*>(result.scanLine(y));
            output[x] =
                qRgba(static_cast<int>(sum[0] / diameter), static_cast<int>(sum[1] / diameter),
                      static_cast<int>(sum[2] / diameter), static_cast<int>(sum[3] / diameter));
            const int removedY = std::clamp(y - radius, 0, height - 1);
            const int addedY = std::clamp(y + radius + 1, 0, height - 1);
            const auto* removedRow = reinterpret_cast<const QRgb*>(source.constScanLine(removedY));
            const auto* addedRow = reinterpret_cast<const QRgb*>(source.constScanLine(addedY));
            const QRgb removed = removedRow[x];
            const QRgb added = addedRow[x];
            sum[0] += qRed(added) - qRed(removed);
            sum[1] += qGreen(added) - qGreen(removed);
            sum[2] += qBlue(added) - qBlue(removed);
            sum[3] += qAlpha(added) - qAlpha(removed);
        }
    }
    return result;
}

QImage gaussianApproximation(const QImage& source, const int blurRadius) {
    if (blurRadius <= 0 || source.isNull()) {
        return source;
    }
    const int passRadius = std::max(1, qRound(blurRadius / 3.0));
    QImage result = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int pass = 0; pass < 3; ++pass) {
        result = verticalBoxBlur(horizontalBoxBlur(result, passRadius), passRadius);
    }
    return result;
}

Sample bilinearSample(const QImage& image, const qreal x, const qreal y) noexcept {
    if (image.isNull()) {
        return {};
    }
    const qreal boundedX = std::clamp(x, 0.0, static_cast<qreal>(image.width() - 1));
    const qreal boundedY = std::clamp(y, 0.0, static_cast<qreal>(image.height() - 1));
    const int left = static_cast<int>(std::floor(boundedX));
    const int top = static_cast<int>(std::floor(boundedY));
    const int right = std::min(left + 1, image.width() - 1);
    const int bottom = std::min(top + 1, image.height() - 1);
    const qreal horizontal = boundedX - left;
    const qreal vertical = boundedY - top;
    const auto* topRow = reinterpret_cast<const QRgb*>(image.constScanLine(top));
    const auto* bottomRow = reinterpret_cast<const QRgb*>(image.constScanLine(bottom));
    const QRgb topLeft = topRow[left];
    const QRgb topRight = topRow[right];
    const QRgb bottomLeft = bottomRow[left];
    const QRgb bottomRight = bottomRow[right];

    const auto interpolate = [horizontal, vertical](const int a, const int b, const int c,
                                                    const int d) {
        const qreal upper = std::lerp(static_cast<qreal>(a), static_cast<qreal>(b), horizontal);
        const qreal lower = std::lerp(static_cast<qreal>(c), static_cast<qreal>(d), horizontal);
        return std::lerp(upper, lower, vertical);
    };
    return {
        interpolate(qRed(topLeft), qRed(topRight), qRed(bottomLeft), qRed(bottomRight)),
        interpolate(qGreen(topLeft), qGreen(topRight), qGreen(bottomLeft), qGreen(bottomRight)),
        interpolate(qBlue(topLeft), qBlue(topRight), qBlue(bottomLeft), qBlue(bottomRight)),
        interpolate(qAlpha(topLeft), qAlpha(topRight), qAlpha(bottomLeft), qAlpha(bottomRight)),
    };
}

qreal averageLuminance(const QImage& image) noexcept {
    if (image.isNull()) {
        return 0.5;
    }
    const int stride = std::max(1, std::min(image.width(), image.height()) / 18);
    qreal luminance = 0.0;
    int samples = 0;
    for (int y = stride / 2; y < image.height(); y += stride) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = stride / 2; x < image.width(); x += stride) {
            const QRgb pixel = row[x];
            luminance +=
                (0.2126 * qRed(pixel) + 0.7152 * qGreen(pixel) + 0.0722 * qBlue(pixel)) / 255.0;
            ++samples;
        }
    }
    return samples > 0 ? luminance / samples : 0.5;
}

int boundedChannel(const qreal channel) noexcept {
    return qRound(std::clamp(channel, 0.0, 255.0));
}

VLiquidGlassQuality resolvedQuality(const VLiquidGlassQuality quality) noexcept {
    switch (quality) {
    case VLiquidGlassQuality::Reduced:
    case VLiquidGlassQuality::Balanced:
    case VLiquidGlassQuality::High:
        return quality;
    case VLiquidGlassQuality::Automatic:
    default:
        return VLiquidGlassQuality::Balanced;
    }
}

} // namespace

int VkLiquidGlassRenderer::capturePadding(const VLiquidGlassStyle& style) noexcept {
    return qCeil(
        std::max({style.blurRadius * 1.35, style.refractionHeight + style.refractionAmount, 2.0}));
}

qreal VkLiquidGlassRenderer::sampleScale(const VLiquidGlassQuality quality,
                                         const qreal devicePixelRatio) noexcept {
    const qreal density = std::clamp(devicePixelRatio, 1.0, 2.5);
    switch (resolvedQuality(quality)) {
    case VLiquidGlassQuality::Reduced:
        return std::clamp(density * 0.34, 0.40, 0.70);
    case VLiquidGlassQuality::High:
        return std::clamp(density * 0.75, 0.85, 1.50);
    case VLiquidGlassQuality::Balanced:
    case VLiquidGlassQuality::Automatic:
    default:
        return std::clamp(density * 0.50, 0.55, 1.0);
    }
}

QImage VkLiquidGlassRenderer::render(const VkLiquidGlassFrame& frame, const QSize& logicalSize,
                                     const VLiquidGlassStyle& style, const QColor& tint) {
    if (frame.image.isNull() || logicalSize.isEmpty() || frame.sampleScale <= 0.0) {
        return {};
    }

    const qreal scale = frame.sampleScale;
    const QSize outputSize(std::max(1, qCeil(logicalSize.width() * scale)),
                           std::max(1, qCeil(logicalSize.height() * scale)));
    const int blurRadius = qRound(std::max<qreal>(0.0, style.blurRadius) * scale);
    const QImage blurred = gaussianApproximation(frame.image, blurRadius);
    QImage result(outputSize, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    const qreal halfWidth = outputSize.width() * 0.5;
    const qreal halfHeight = outputSize.height() * 0.5;
    const qreal maximumRadius = std::min(halfWidth, halfHeight);
    const qreal radius = style.cornerRadius < 0.0
                             ? maximumRadius
                             : std::clamp(style.cornerRadius * scale, 0.0, maximumRadius);
    const qreal refractionHeight = std::max<qreal>(0.0, style.refractionHeight) * scale;
    const qreal refractionAmount = std::max<qreal>(0.0, style.refractionAmount) * scale;
    const qreal dispersion = std::max<qreal>(0.0, style.chromaticAberration) * scale;
    const qreal padding = frame.padding * scale;
    const qreal saturation = std::clamp(style.saturation, 0.0, 2.0);
    qreal tintOpacity = std::clamp(style.tintOpacity, 0.0, 1.0);
    if (style.adaptiveLuminance) {
        const qreal contrastDistance = std::abs(averageLuminance(blurred) - 0.5) * 2.0;
        tintOpacity *= std::lerp(0.78, 1.18, contrastDistance);
        tintOpacity = std::clamp(tintOpacity, 0.0, 1.0);
    }

    for (int y = 0; y < outputSize.height(); ++y) {
        auto* output = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < outputSize.width(); ++x) {
            const qreal centeredX = x + 0.5 - halfWidth;
            const qreal centeredY = y + 0.5 - halfHeight;
            const qreal distance =
                roundedRectDistance(centeredX, centeredY, halfWidth, halfHeight, radius);
            if (distance > 0.0) {
                continue;
            }

            qreal normalX = 0.0;
            qreal normalY = 0.0;
            qreal displacement = 0.0;
            qreal edgeDispersion = 0.0;
            const qreal edgeDepth = -distance;
            if (refractionHeight > 0.0 && edgeDepth < refractionHeight) {
                const qreal edgeProgress = 1.0 - edgeDepth / refractionHeight;
                const qreal lens =
                    1.0 - std::sqrt(std::max<qreal>(0.0, 1.0 - edgeProgress * edgeProgress));
                normalX =
                    roundedRectDistance(centeredX + 0.5, centeredY, halfWidth, halfHeight, radius) -
                    roundedRectDistance(centeredX - 0.5, centeredY, halfWidth, halfHeight, radius);
                normalY =
                    roundedRectDistance(centeredX, centeredY + 0.5, halfWidth, halfHeight, radius) -
                    roundedRectDistance(centeredX, centeredY - 0.5, halfWidth, halfHeight, radius);
                const qreal normalLength = std::hypot(normalX, normalY);
                if (normalLength > 0.0001) {
                    normalX /= normalLength;
                    normalY /= normalLength;
                }
                displacement = lens * refractionAmount;
                edgeDispersion = lens * dispersion;
            }

            const qreal sampleX = padding + x - normalX * displacement;
            const qreal sampleY = padding + y - normalY * displacement;
            const Sample center = bilinearSample(blurred, sampleX, sampleY);
            const Sample redSample = bilinearSample(blurred, sampleX - normalX * edgeDispersion,
                                                    sampleY - normalY * edgeDispersion);
            const Sample blueSample = bilinearSample(blurred, sampleX + normalX * edgeDispersion,
                                                     sampleY + normalY * edgeDispersion);

            qreal red = redSample.red;
            qreal green = center.green;
            qreal blue = blueSample.blue;
            const qreal gray = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
            red = gray + (red - gray) * saturation;
            green = gray + (green - gray) * saturation;
            blue = gray + (blue - gray) * saturation;
            red = std::lerp(red, static_cast<qreal>(tint.red()), tintOpacity);
            green = std::lerp(green, static_cast<qreal>(tint.green()), tintOpacity);
            blue = std::lerp(blue, static_cast<qreal>(tint.blue()), tintOpacity);
            output[x] = qRgba(boundedChannel(red), boundedChannel(green), boundedChannel(blue),
                              boundedChannel(center.alpha));
        }
    }
    return result;
}

} // namespace vkui
