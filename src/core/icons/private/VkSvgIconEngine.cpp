// SPDX-License-Identifier: MIT

#include "VkIconCache_p.h"
#include "VkIconMaskCache_p.h"
#include "VkSvgIconEngine_p.h"
#include "VkSvgIconSourceCache_p.h"

#include <QtCore/QtMath>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>

namespace vkui {
namespace {

QColor blend(const QColor& foreground, const QColor& background, const qreal backgroundAmount) {
    const qreal amount = std::clamp(backgroundAmount, 0.0, 1.0);
    return QColor(qRound(foreground.red() * (1.0 - amount) + background.red() * amount),
                  qRound(foreground.green() * (1.0 - amount) + background.green() * amount),
                  qRound(foreground.blue() * (1.0 - amount) + background.blue() * amount),
                  qRound(foreground.alpha() * (1.0 - amount) + background.alpha() * amount));
}

QColor contrastingColor(const QColor& background) {
    const auto linearChannel = [](const qreal value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    const qreal luminance = 0.2126 * linearChannel(background.redF()) +
                            0.7152 * linearChannel(background.greenF()) +
                            0.0722 * linearChannel(background.blueF());
    const qreal whiteContrast = 1.05 / (luminance + 0.05);
    const qreal blackContrast = (luminance + 0.05) / 0.05;
    return whiteContrast >= blackContrast ? QColor(Qt::white) : QColor(Qt::black);
}

struct ChannelColors final {
    QColor primary;
    QColor secondary;
};

ChannelColors channelColors(const VkColorTokens& tokens, const VkIconRole role,
                            const QIcon::Mode mode, const QIcon::State state) {
    ChannelColors colors;
    switch (role) {
    case VkIconRole::Primary:
        colors = {tokens.symbolPrimary, tokens.symbolSecondary};
        break;
    case VkIconRole::Secondary:
        colors = {tokens.symbolSecondary,
                  blend(tokens.symbolSecondary, tokens.contentBackground, 0.42)};
        break;
    case VkIconRole::Disabled:
        colors = {tokens.symbolDisabled,
                  blend(tokens.symbolDisabled, tokens.contentBackground, 0.45)};
        break;
    case VkIconRole::Accent:
        colors = {tokens.accent, blend(tokens.accent, tokens.contentBackground, 0.38)};
        break;
    case VkIconRole::Destructive:
        colors = {tokens.destructive, blend(tokens.destructive, tokens.contentBackground, 0.38)};
        break;
    }

    if (state == QIcon::On && role != VkIconRole::Disabled) {
        colors.primary = mode == QIcon::Active ? tokens.accentHovered : tokens.accent;
        colors.secondary = blend(colors.primary, tokens.contentBackground, 0.38);
    }

    if (mode == QIcon::Active) {
        if (role == VkIconRole::Accent || state == QIcon::On) {
            colors.primary = tokens.accentHovered;
            colors.secondary = blend(tokens.accentHovered, tokens.contentBackground, 0.34);
        } else if (role == VkIconRole::Destructive) {
            colors.primary = blend(tokens.destructive, tokens.textPrimary, 0.12);
        }
    } else if (mode == QIcon::Selected) {
        colors.primary = contrastingColor(tokens.accent);
        colors.secondary = blend(colors.primary, tokens.accent, 0.28);
    }

    if (mode == QIcon::Disabled || role == VkIconRole::Disabled) {
        colors.primary = tokens.symbolDisabled;
        colors.secondary = blend(tokens.symbolDisabled, tokens.contentBackground, 0.45);
    }
    return colors;
}

ChannelColors applicationPaletteColors(const QPalette& palette, const QPalette::ColorRole role,
                                       const QPalette::ColorGroup requestedGroup,
                                       const QIcon::Mode mode) {
    QPalette::ColorGroup group = requestedGroup;
    if (mode == QIcon::Disabled) {
        group = QPalette::Disabled;
    }

    QPalette::ColorRole resolvedRole = role;
    if (mode == QIcon::Selected) {
        switch (role) {
        case QPalette::WindowText:
        case QPalette::Text:
        case QPalette::ButtonText:
        case QPalette::ToolTipText:
        case QPalette::PlaceholderText:
            resolvedRole = QPalette::HighlightedText;
            break;
        default:
            break;
        }
    }

    const QColor primary = palette.color(group, resolvedRole);
    return {primary, blend(primary, palette.color(group, QPalette::Window), 0.42)};
}

QImage renderSemanticMask(QByteArray source, const QSize& physicalSize) {
    // Red and green encode the two semantic channels in one renderer pass. The premultiplied
    // result preserves antialias coverage and SVG paint order while remaining color-independent.
    source.replace("#000001", "#ff0000");
    source.replace("#000002", "#00ff00");
    QSvgRenderer renderer(source);
    if (!renderer.isValid()) {
        return {};
    }

    QImage mask(physicalSize, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(physicalSize)));
    painter.end();
    return mask;
}

int multiplyChannel(const int first, const int second) {
    return (first * second + 127) / 255;
}

QImage colorizeSemanticMask(const QImage& mask, const ChannelColors& colors) {
    if (mask.isNull()) {
        return {};
    }

    QImage image(mask.size(), QImage::Format_ARGB32_Premultiplied);
    const QColor primary = colors.primary.toRgb();
    const QColor secondary = colors.secondary.toRgb();
    for (int y = 0; y < mask.height(); ++y) {
        const auto* source = reinterpret_cast<const QRgb*>(mask.constScanLine(y));
        auto* destination = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < mask.width(); ++x) {
            const int primaryCoverage = qRed(source[x]);
            const int secondaryCoverage = qGreen(source[x]);
            const int primaryWeight = multiplyChannel(primaryCoverage, primary.alpha());
            const int secondaryWeight = multiplyChannel(secondaryCoverage, secondary.alpha());
            const int alpha = std::min(255, primaryWeight + secondaryWeight);
            const int red = multiplyChannel(primary.red(), primaryWeight) +
                            multiplyChannel(secondary.red(), secondaryWeight);
            const int green = multiplyChannel(primary.green(), primaryWeight) +
                              multiplyChannel(secondary.green(), secondaryWeight);
            const int blue = multiplyChannel(primary.blue(), primaryWeight) +
                             multiplyChannel(secondary.blue(), secondaryWeight);
            destination[x] = qRgba(std::min(255, red), std::min(255, green),
                                   std::min(255, blue), alpha);
        }
    }
    return image;
}

qreal normalizedDevicePixelRatio(const qreal value) {
    if (!qIsFinite(value) || value <= 0.0) {
        return 1.0;
    }
    return std::clamp(value, 1.0, 16.0);
}

qint64 encodedDevicePixelRatio(const qreal value) {
    return qRound64(normalizedDevicePixelRatio(value) * 1024.0);
}

} // namespace

VkSvgIconEngine::VkSvgIconEngine(const VkSymbol symbol, const VkIconRole role)
    : symbol_(symbol), role_(role), source_(VkSvgIconSourceCache::source(symbol)) {}

VkSvgIconEngine::VkSvgIconEngine(const VkSymbol symbol, const QPalette::ColorRole role,
                                 const QPalette::ColorGroup group)
    : symbol_(symbol), role_(VkIconRole::Primary), paletteRole_(role), paletteGroup_(group),
      usesApplicationPalette_(true), source_(VkSvgIconSourceCache::source(symbol)) {}

VkSvgIconEngine::VkSvgIconEngine(const VkSymbol symbol, QColor primary, QColor secondary)
    : symbol_(symbol), role_(VkIconRole::Primary), explicitPrimary_(std::move(primary)),
      explicitSecondary_(std::move(secondary)), usesExplicitColors_(true),
      source_(VkSvgIconSourceCache::source(symbol)) {
    if (!explicitSecondary_.isValid()) {
        const QColor presumedSurface =
            explicitPrimary_.lightnessF() > 0.5 ? QColor(Qt::black) : QColor(Qt::white);
        explicitSecondary_ = blend(explicitPrimary_, presumedSurface, 0.42);
    }
}

QIconEngine* VkSvgIconEngine::clone() const {
    return new VkSvgIconEngine(*this);
}

QString VkSvgIconEngine::key() const {
    return QStringLiteral("vkui-svg-icon");
}

QSize VkSvgIconEngine::actualSize(const QSize& size, QIcon::Mode, QIcon::State) {
    if (size.isEmpty() || !source_ || source_->intrinsicSize.isEmpty()) {
        return {};
    }
    return source_->intrinsicSize.scaled(size, Qt::KeepAspectRatio);
}

QPixmap VkSvgIconEngine::pixmap(const QSize& size, const QIcon::Mode mode,
                                const QIcon::State state) {
    return renderPixmap(size, 1.0, mode, state);
}

QPixmap VkSvgIconEngine::scaledPixmap(const QSize& size, const QIcon::Mode mode,
                                      const QIcon::State state, const qreal scale) {
    QSize logicalSize = size;
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    // QIcon passed a device-pixel size here before Qt 6.8. Normalize it so the
    // engine's cache and rasterizer consistently operate on logical sizes.
    if (scale > 0.0 && !qFuzzyCompare(scale, 1.0)) {
        logicalSize = QSize(std::max(1, qRound(size.width() / scale)),
                            std::max(1, qRound(size.height() / scale)));
    }
#endif
    return renderPixmap(logicalSize, scale, mode, state);
}

void VkSvgIconEngine::paint(QPainter* painter, const QRect& rect, const QIcon::Mode mode,
                            const QIcon::State state) {
    if (painter == nullptr || rect.isEmpty() || !source_) {
        return;
    }

    const QSize logicalSize = actualSize(rect.size(), mode, state);
    if (logicalSize.isEmpty()) {
        return;
    }

    const qreal devicePixelRatio =
        painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
    const QPixmap rendered = renderPixmap(logicalSize, devicePixelRatio, mode, state);
    if (rendered.isNull()) {
        return;
    }

    const QRect target(QPoint(rect.x() + (rect.width() - logicalSize.width()) / 2,
                              rect.y() + (rect.height() - logicalSize.height()) / 2),
                       logicalSize);
    painter->save();
    painter->drawPixmap(target, rendered);
    painter->restore();
}

QPixmap VkSvgIconEngine::renderPixmap(const QSize& requestedSize,
                                      const qreal requestedDevicePixelRatio, const QIcon::Mode mode,
                                      const QIcon::State state) const {
    if (!source_ || requestedSize.isEmpty()) {
        return {};
    }

    const QSize logicalSize = source_->intrinsicSize.scaled(requestedSize, Qt::KeepAspectRatio);
    if (logicalSize.isEmpty()) {
        return {};
    }

    const VkTheme* theme = nullptr;
    std::optional<QPalette> applicationPalette;
    if (usesApplicationPalette_) {
        applicationPalette.emplace(QGuiApplication::palette());
    } else if (!usesExplicitColors_) {
        theme = &VkThemeManager::instance()->theme();
    }

    VkIconCacheKey cacheKey;
    cacheKey.symbol = symbol_;
    cacheKey.role = role_;
    cacheKey.size = logicalSize;
    cacheKey.devicePixelRatio = encodedDevicePixelRatio(requestedDevicePixelRatio);
    cacheKey.mode = mode;
    cacheKey.state = state;
    cacheKey.colorGeneration =
        usesExplicitColors_
            ? 0
            : (usesApplicationPalette_ ? static_cast<quint64>(applicationPalette->cacheKey())
                                       : theme->colorGeneration());
    if (usesExplicitColors_) {
        cacheKey.colorIdentity = (static_cast<quint64>(explicitPrimary_.rgba()) << 32U) |
                                 static_cast<quint64>(explicitSecondary_.rgba());
    } else if (usesApplicationPalette_) {
        cacheKey.colorIdentity =
            (static_cast<quint64>(paletteGroup_) << 32U) | static_cast<quint64>(paletteRole_);
    }

    QPixmap cached;
    if (VkIconCache::instance().lookup(cacheKey, &cached)) {
        return cached;
    }

    const qreal devicePixelRatio = static_cast<qreal>(cacheKey.devicePixelRatio) / 1024.0;
    const QSize physicalSize(std::max(1, qCeil(logicalSize.width() * devicePixelRatio)),
                             std::max(1, qCeil(logicalSize.height() * devicePixelRatio)));
    ChannelColors colors =
        usesExplicitColors_
            ? ChannelColors{explicitPrimary_, explicitSecondary_}
            : (usesApplicationPalette_
                   ? applicationPaletteColors(*applicationPalette, paletteRole_, paletteGroup_, mode)
                   : channelColors(theme->colors(), role_, mode, state));
    if (usesExplicitColors_ && mode == QIcon::Disabled) {
        colors.primary = colors.secondary;
    }

    const VkIconMaskCacheKey maskKey{symbol_, physicalSize};
    QImage mask;
    if (!VkIconMaskCache::instance().lookup(maskKey, &mask)) {
        mask = renderSemanticMask(source_->source, physicalSize);
        VkIconMaskCache::instance().insert(maskKey, mask);
    }
    QImage image = colorizeSemanticMask(mask, colors);
    if (image.isNull()) {
        return {};
    }

    QPixmap result = QPixmap::fromImage(std::move(image));
    result.setDevicePixelRatio(devicePixelRatio);
    VkIconCache::instance().insert(cacheKey, result);
    return result;
}

} // namespace vkui
