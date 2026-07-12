// SPDX-License-Identifier: MIT

#include "VkIconCache_p.h"
#include "VkSvgIconEngine_p.h"

#include <QtCore/QFile>
#include <QtCore/QtMath>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>

namespace vkui {
namespace {

QString symbolName(const VkSymbol symbol) {
    switch (symbol) {
    case VkSymbol::ChevronLeft:
        return QStringLiteral("chevron-left");
    case VkSymbol::ChevronRight:
        return QStringLiteral("chevron-right");
    case VkSymbol::ChevronUp:
        return QStringLiteral("chevron-up");
    case VkSymbol::ChevronDown:
        return QStringLiteral("chevron-down");
    case VkSymbol::Plus:
        return QStringLiteral("plus");
    case VkSymbol::Minus:
        return QStringLiteral("minus");
    case VkSymbol::Close:
        return QStringLiteral("close");
    case VkSymbol::Checkmark:
        return QStringLiteral("checkmark");
    case VkSymbol::Information:
        return QStringLiteral("information");
    case VkSymbol::Warning:
        return QStringLiteral("warning");
    case VkSymbol::Settings:
        return QStringLiteral("settings");
    case VkSymbol::Search:
        return QStringLiteral("search");
    case VkSymbol::Folder:
        return QStringLiteral("folder");
    case VkSymbol::Document:
        return QStringLiteral("document");
    case VkSymbol::Share:
        return QStringLiteral("share");
    case VkSymbol::More:
        return QStringLiteral("more");
    case VkSymbol::ToggleOff:
        return QStringLiteral("toggle-off");
    case VkSymbol::ToggleOn:
        return QStringLiteral("toggle-on");
    case VkSymbol::Power:
        return QStringLiteral("power");
    case VkSymbol::Sidebar:
        return QStringLiteral("sidebar");
    case VkSymbol::Grid:
        return QStringLiteral("grid");
    case VkSymbol::List:
        return QStringLiteral("list");
    case VkSymbol::Edit:
        return QStringLiteral("edit");
    case VkSymbol::Trash:
        return QStringLiteral("trash");
    case VkSymbol::Download:
        return QStringLiteral("download");
    case VkSymbol::Upload:
        return QStringLiteral("upload");
    case VkSymbol::Lock:
        return QStringLiteral("lock");
    case VkSymbol::Eye:
        return QStringLiteral("eye");
    case VkSymbol::Save:
        return QStringLiteral("save");
    case VkSymbol::Reset:
        return QStringLiteral("reset");
    case VkSymbol::Duplicate:
        return QStringLiteral("duplicate");
    case VkSymbol::Image:
        return QStringLiteral("image");
    case VkSymbol::Background:
        return QStringLiteral("background");
    case VkSymbol::Focus:
        return QStringLiteral("focus");
    case VkSymbol::Rename:
        return QStringLiteral("rename");
    case VkSymbol::Projects:
        return QStringLiteral("projects");
    case VkSymbol::Remove:
        return QStringLiteral("remove");
    case VkSymbol::Reveal:
        return QStringLiteral("reveal");
    }
    return {};
}

QByteArray loadSource(const VkSymbol symbol) {
    const QString name = symbolName(symbol);
    if (name.isEmpty()) {
        return {};
    }

    QFile file(QStringLiteral(":/vkui/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QColor blend(const QColor& foreground, const QColor& background, const qreal backgroundAmount) {
    const qreal amount = std::clamp(backgroundAmount, 0.0, 1.0);
    return QColor(qRound(foreground.red() * (1.0 - amount) + background.red() * amount),
                  qRound(foreground.green() * (1.0 - amount) + background.green() * amount),
                  qRound(foreground.blue() * (1.0 - amount) + background.blue() * amount));
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

QByteArray coloredSource(QByteArray source, const ChannelColors& colors) {
    // The source assets use two deliberately impossible near-black literals.
    // Replacing the complete six-digit tokens works for both fill and stroke
    // attributes without imposing a particular SVG element structure.
    source.replace("#000001", colors.primary.toRgb().name(QColor::HexRgb).toUtf8());
    source.replace("#000002", colors.secondary.toRgb().name(QColor::HexRgb).toUtf8());
    return source;
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
    : symbol_(symbol), role_(role), source_(loadSource(symbol)) {
    if (!source_.isEmpty()) {
        const QSvgRenderer renderer(source_);
        if (renderer.isValid()) {
            intrinsicSize_ = renderer.defaultSize();
        }
    }
    if (intrinsicSize_.isEmpty()) {
        intrinsicSize_ = QSize(24, 24);
    }
}

QIconEngine* VkSvgIconEngine::clone() const {
    return new VkSvgIconEngine(symbol_, role_);
}

QString VkSvgIconEngine::key() const {
    return QStringLiteral("vkui-svg-icon");
}

QSize VkSvgIconEngine::actualSize(const QSize& size, QIcon::Mode, QIcon::State) {
    if (size.isEmpty() || intrinsicSize_.isEmpty()) {
        return {};
    }
    return intrinsicSize_.scaled(size, Qt::KeepAspectRatio);
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
    if (painter == nullptr || rect.isEmpty() || source_.isEmpty()) {
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
    if (source_.isEmpty() || requestedSize.isEmpty()) {
        return {};
    }

    const QSize logicalSize = intrinsicSize_.scaled(requestedSize, Qt::KeepAspectRatio);
    if (logicalSize.isEmpty()) {
        return {};
    }

    const VkTheme& theme = VkThemeManager::instance()->theme();
    VkIconCacheKey cacheKey;
    cacheKey.symbol = symbol_;
    cacheKey.role = role_;
    cacheKey.size = logicalSize;
    cacheKey.devicePixelRatio = encodedDevicePixelRatio(requestedDevicePixelRatio);
    cacheKey.mode = mode;
    cacheKey.state = state;
    cacheKey.themeGeneration = theme.generation();

    QPixmap cached;
    if (VkIconCache::instance().lookup(cacheKey, &cached)) {
        return cached;
    }

    const qreal devicePixelRatio = static_cast<qreal>(cacheKey.devicePixelRatio) / 1024.0;
    const QSize physicalSize(std::max(1, qCeil(logicalSize.width() * devicePixelRatio)),
                             std::max(1, qCeil(logicalSize.height() * devicePixelRatio)));
    QImage image(physicalSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const ChannelColors colors = channelColors(theme.colors(), role_, mode, state);
    const QByteArray svg = coloredSource(source_, colors);
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return {};
    }

    QPainter imagePainter(&image);
    imagePainter.setRenderHint(QPainter::Antialiasing, true);
    imagePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&imagePainter, QRectF(QPointF(0.0, 0.0), QSizeF(physicalSize)));
    imagePainter.end();

    QPixmap result = QPixmap::fromImage(std::move(image));
    result.setDevicePixelRatio(devicePixelRatio);
    VkIconCache::instance().insert(cacheKey, result);
    return result;
}

} // namespace vkui
