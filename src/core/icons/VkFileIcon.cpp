// SPDX-License-Identifier: MIT

#include "private/VkResourceInitializer_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <QtCore/QtMath>
#include <QtGui/QFontDatabase>
#include <QtGui/QFontMetricsF>
#include <QtGui/QGuiApplication>
#include <QtGui/QIconEngine>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QPixmapCache>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <utility>
#include <vkui/core/VkFileIcon.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>

namespace vkui {
namespace {

constexpr auto kBundledFontPath = ":/vkui/fonts/FiraCodeNerdFont-Regular.ttf";

struct FileIconFontState final {
    std::once_flag registrationFlag;
    std::atomic_bool registrationFinished = false;
    QString family;
    bool available = false;
};

FileIconFontState& fileIconFontState() {
    static FileIconFontState state;
    return state;
}

char32_t glyphCodePoint(const VkFileGlyph glyph) noexcept {
    switch (glyph) {
    case VkFileGlyph::FolderClosed:
        return 0xf07b;
    case VkFileGlyph::FolderOpen:
        return 0xf07c;
    case VkFileGlyph::File:
        return 0xf15b;
    case VkFileGlyph::TextFile:
        return 0xf15c;
    case VkFileGlyph::CodeFile:
        return 0xf1c9;
    case VkFileGlyph::ImageFile:
        return 0xf1c5;
    case VkFileGlyph::PdfFile:
        return 0xf1c1;
    case VkFileGlyph::ArchiveFile:
        return 0xf1c6;
    case VkFileGlyph::BookFile:
        return 0xf02d;
    }
    return 0;
}

QString glyphText(const VkFileGlyph glyph) {
    const char32_t codePoint = glyphCodePoint(glyph);
    return codePoint == 0 ? QString{} : QString::fromUcs4(&codePoint, 1);
}

constexpr std::array<VkFileGlyph, 9> kFileGlyphs{
    VkFileGlyph::FolderClosed, VkFileGlyph::FolderOpen,  VkFileGlyph::File,
    VkFileGlyph::TextFile,     VkFileGlyph::CodeFile,    VkFileGlyph::ImageFile,
    VkFileGlyph::PdfFile,      VkFileGlyph::ArchiveFile, VkFileGlyph::BookFile,
};

qreal normalizedDevicePixelRatio(const qreal value) noexcept {
    if (!qIsFinite(value) || value <= 0.0) {
        return 1.0;
    }
    return std::clamp(value, 1.0, 16.0);
}

qint64 encodedDevicePixelRatio(const qreal value) noexcept {
    return qRound64(normalizedDevicePixelRatio(value) * 1024.0);
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

QColor semanticColor(const VkIconRole role, const QIcon::Mode mode, const QIcon::State state) {
    const VkColorTokens& colors = VkThemeManager::instance()->theme().colors();
    if (mode == QIcon::Disabled || role == VkIconRole::Disabled) {
        return colors.symbolDisabled;
    }
    if (mode == QIcon::Selected) {
        return contrastingColor(colors.accent);
    }
    if (state == QIcon::On) {
        return mode == QIcon::Active ? colors.accentHovered : colors.accent;
    }

    switch (role) {
    case VkIconRole::Accent:
        return mode == QIcon::Active ? colors.accentHovered : colors.accent;
    case VkIconRole::Destructive:
        return colors.destructive;
    case VkIconRole::Secondary:
        return colors.symbolSecondary;
    case VkIconRole::Disabled:
        return colors.symbolDisabled;
    case VkIconRole::Primary:
    default:
        return colors.symbolPrimary;
    }
}

QColor applicationPaletteColor(const QPalette::ColorRole role,
                               const QPalette::ColorGroup requestedGroup, const QIcon::Mode mode) {
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
    return QGuiApplication::palette().color(group, resolvedRole);
}

void drawGlyph(QPainter& painter, const QRectF& bounds, const VkFileGlyph glyph,
               const QColor& color) {
    const QString text = glyphText(glyph);
    if (bounds.isEmpty() || text.isEmpty() || !color.isValid()) {
        return;
    }

    const qreal shortestEdge = std::min(bounds.width(), bounds.height());
    const qreal inset = std::clamp(shortestEdge * 0.08, 0.5, 2.0);
    const QRectF available = bounds.adjusted(inset, inset, -inset, -inset);
    if (available.isEmpty()) {
        return;
    }

    int pixelSize = std::max(1, qFloor(available.height() * 0.88));
    QFont font = fileIconFont(pixelSize);
    QFontMetricsF fontMetrics(font, painter.device());
    const auto fitScale = [&] {
        const QRectF ink = fontMetrics.tightBoundingRect(text);
        return std::min(available.width() / std::max<qreal>(1.0, ink.width()),
                        available.height() / std::max<qreal>(1.0, ink.height()));
    };
    const qreal scale = fitScale();
    if (scale < 1.0) {
        pixelSize = std::max(1, qFloor(pixelSize * scale * 0.96));
        font = fileIconFont(pixelSize);
    }
    const QFontMetricsF finalMetrics(font, painter.device());
    const QRectF ink = finalMetrics.tightBoundingRect(text);
    const QPointF baseline(available.center().x() - ink.center().x(),
                           available.center().y() - ink.center().y());

    painter.save();
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(baseline, text);
    painter.restore();
}

enum class ColorSource {
    Semantic,
    ApplicationPalette,
    Explicit,
};

class FileGlyphIconEngine final : public QIconEngine {
  public:
    FileGlyphIconEngine(const VkFileGlyph glyph, const VkIconRole role)
        : glyph_(glyph), semanticRole_(role) {}

    FileGlyphIconEngine(const VkFileGlyph glyph, const QPalette::ColorRole role,
                        const QPalette::ColorGroup group)
        : glyph_(glyph), paletteRole_(role), paletteGroup_(group),
          colorSource_(ColorSource::ApplicationPalette) {}

    FileGlyphIconEngine(const VkFileGlyph glyph, QColor color)
        : glyph_(glyph), explicitColor_(std::move(color)), colorSource_(ColorSource::Explicit) {}

    [[nodiscard]] QIconEngine* clone() const override {
        switch (colorSource_) {
        case ColorSource::ApplicationPalette:
            return new FileGlyphIconEngine(glyph_, paletteRole_, paletteGroup_);
        case ColorSource::Explicit:
            return new FileGlyphIconEngine(glyph_, explicitColor_);
        case ColorSource::Semantic:
        default:
            return new FileGlyphIconEngine(glyph_, semanticRole_);
        }
    }

    [[nodiscard]] QString key() const override {
        return QStringLiteral("vkui-file-glyph-icon");
    }

    [[nodiscard]] QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override {
        return size;
    }

    [[nodiscard]] QPixmap pixmap(const QSize& size, const QIcon::Mode mode,
                                 const QIcon::State state) override {
        return renderPixmap(size, 1.0, mode, state);
    }

    [[nodiscard]] QPixmap scaledPixmap(const QSize& size, const QIcon::Mode mode,
                                       const QIcon::State state, const qreal scale) override {
        QSize logicalSize = size;
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
        // QIcon passed device-pixel dimensions before Qt 6.8.
        if (scale > 0.0 && !qFuzzyCompare(scale, 1.0)) {
            logicalSize = QSize(std::max(1, qRound(size.width() / scale)),
                                std::max(1, qRound(size.height() / scale)));
        }
#endif
        return renderPixmap(logicalSize, scale, mode, state);
    }

    void paint(QPainter* painter, const QRect& rect, const QIcon::Mode mode,
               const QIcon::State state) override {
        if (painter == nullptr || rect.isEmpty()) {
            return;
        }
        drawGlyph(*painter, rect, glyph_, resolvedColor(mode, state));
    }

  private:
    [[nodiscard]] QColor resolvedColor(const QIcon::Mode mode, const QIcon::State state) const {
        switch (colorSource_) {
        case ColorSource::ApplicationPalette:
            return applicationPaletteColor(paletteRole_, paletteGroup_, mode);
        case ColorSource::Explicit: {
            QColor color = explicitColor_;
            if (mode == QIcon::Disabled) {
                color.setAlphaF(color.alphaF() * 0.38F);
            }
            return color;
        }
        case ColorSource::Semantic:
        default:
            return semanticColor(semanticRole_, mode, state);
        }
    }

    [[nodiscard]] quint64 colorGeneration() const {
        switch (colorSource_) {
        case ColorSource::ApplicationPalette:
            return static_cast<quint64>(QGuiApplication::palette().cacheKey());
        case ColorSource::Explicit:
            return explicitColor_.rgba();
        case ColorSource::Semantic:
        default:
            return VkThemeManager::instance()->theme().generation();
        }
    }

    [[nodiscard]] quint64 colorIdentity() const noexcept {
        switch (colorSource_) {
        case ColorSource::ApplicationPalette:
            return (static_cast<quint64>(paletteGroup_) << 32U) |
                   static_cast<quint64>(paletteRole_);
        case ColorSource::Explicit:
            return explicitColor_.rgba();
        case ColorSource::Semantic:
        default:
            return static_cast<quint64>(semanticRole_);
        }
    }

    [[nodiscard]] QPixmap renderPixmap(const QSize& requestedSize, const qreal requestedDpr,
                                       const QIcon::Mode mode, const QIcon::State state) const {
        if (requestedSize.isEmpty() || glyphCodePoint(glyph_) == 0) {
            return {};
        }

        const qreal dpr = normalizedDevicePixelRatio(requestedDpr);
        const QSize physicalSize(std::max(1, qCeil(requestedSize.width() * dpr)),
                                 std::max(1, qCeil(requestedSize.height() * dpr)));
        const QString cacheKey = QStringLiteral("vkui.file-glyph.%1.%2.%3.%4x%5.%6.%7.%8.%9")
                                     .arg(static_cast<int>(glyph_))
                                     .arg(static_cast<int>(colorSource_))
                                     .arg(colorIdentity())
                                     .arg(requestedSize.width())
                                     .arg(requestedSize.height())
                                     .arg(encodedDevicePixelRatio(dpr))
                                     .arg(static_cast<int>(mode))
                                     .arg(static_cast<int>(state))
                                     .arg(colorGeneration());

        QPixmap cached;
        if (QPixmapCache::find(cacheKey, &cached)) {
            return cached;
        }

        QImage image(physicalSize, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        image.setDevicePixelRatio(dpr);
        QPainter painter(&image);
        drawGlyph(painter, QRectF(QPointF(0.0, 0.0), QSizeF(requestedSize)), glyph_,
                  resolvedColor(mode, state));
        painter.end();

        QPixmap result = QPixmap::fromImage(std::move(image));
        result.setDevicePixelRatio(dpr);
        QPixmapCache::insert(cacheKey, result);
        return result;
    }

    VkFileGlyph glyph_;
    VkIconRole semanticRole_ = VkIconRole::Secondary;
    QPalette::ColorRole paletteRole_ = QPalette::Text;
    QPalette::ColorGroup paletteGroup_ = QPalette::Active;
    QColor explicitColor_;
    ColorSource colorSource_ = ColorSource::Semantic;
};

} // namespace

bool initializeFileIconFont() {
    FileIconFontState& state = fileIconFontState();
    if (state.registrationFinished.load(std::memory_order_acquire)) {
        return state.available;
    }

    QCoreApplication* application = QCoreApplication::instance();
    if (application == nullptr || qobject_cast<QGuiApplication*>(application) == nullptr ||
        QThread::currentThread() != application->thread()) {
        return false;
    }

    std::call_once(state.registrationFlag, [&state] {
        detail::ensureResourcesInitialized();
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(kBundledFontPath));
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (!families.isEmpty()) {
            state.family = families.constFirst();
            state.available = true;
        }
        state.registrationFinished.store(true, std::memory_order_release);
    });
    return state.available;
}

QString fileIconFontFamily() {
    return initializeFileIconFont() ? fileIconFontState().family : QString{};
}

QFont fileIconFont(const int pixelSize) {
    QFont font;
    const QString family = fileIconFontFamily();
    if (!family.isEmpty()) {
        font.setFamily(family);
    } else {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPixelSize(std::max(1, pixelSize));
    return font;
}

VkFileIconMetrics fileIconMetrics(const QFont& interfaceFont,
                                  const qreal requestedDevicePixelRatio) {
    const QFontMetricsF interfaceMetrics(interfaceFont);
    const qreal interfaceHeight = std::max<qreal>(1.0, interfaceMetrics.height());
    const int glyphPixelSize = std::max(1, qRound(interfaceHeight * 0.88));
    const QFont glyphFont = fileIconFont(glyphPixelSize);
    const QFontMetricsF glyphMetrics(glyphFont);

    qreal maximumWidth = 1.0;
    qreal maximumHeight = 1.0;
    for (const VkFileGlyph glyph : kFileGlyphs) {
        const QString text = glyphText(glyph);
        maximumWidth =
            std::max(maximumWidth, std::max(glyphMetrics.horizontalAdvance(text),
                                            glyphMetrics.tightBoundingRect(text).width()));
        maximumHeight = std::max(maximumHeight, glyphMetrics.height());
    }

    VkFileIconMetrics result;
    result.glyphPixelSize = glyphPixelSize;
    result.glyphSlotSize =
        QSize(std::max(1, qCeil(maximumWidth + 2.0)),
              std::max(1, qCeil(std::max(interfaceHeight, maximumHeight) + 2.0)));
    result.textGap = std::max(2, qRound(interfaceHeight * 0.2));
    result.rowHeight =
        std::max(result.glyphSlotSize.height(),
                 qCeil(interfaceHeight + std::max<qreal>(4.0, interfaceHeight * 0.38)));
    result.devicePixelRatio = normalizedDevicePixelRatio(requestedDevicePixelRatio);
    result.devicePixelGlyphSlotSize =
        QSize(std::max(1, qCeil(result.glyphSlotSize.width() * result.devicePixelRatio)),
              std::max(1, qCeil(result.glyphSlotSize.height() * result.devicePixelRatio)));
    return result;
}

QIcon fileIcon(const VkFileGlyph glyph, const VkIconRole role) {
    if (glyphCodePoint(glyph) == 0) {
        return {};
    }
    (void)initializeFileIconFont();
    return QIcon(new FileGlyphIconEngine(glyph, role));
}

QIcon fileIcon(const VkFileGlyph glyph, const QPalette::ColorRole role,
               const QPalette::ColorGroup group) {
    if (glyphCodePoint(glyph) == 0) {
        return {};
    }
    (void)initializeFileIconFont();
    return QIcon(new FileGlyphIconEngine(glyph, role, group));
}

QIcon fileIcon(const VkFileGlyph glyph, const QColor& color) {
    if (glyphCodePoint(glyph) == 0 || !color.isValid()) {
        return {};
    }
    (void)initializeFileIconFont();
    return QIcon(new FileGlyphIconEngine(glyph, color));
}

void drawFileGlyph(QPainter& painter, const QRectF& bounds, const VkFileGlyph glyph,
                   const QPalette& palette, const QPalette::ColorRole role,
                   const QPalette::ColorGroup group) {
    drawGlyph(painter, bounds, glyph, palette.color(group, role));
}

void drawFileGlyph(QPainter& painter, const QRectF& bounds, const VkFileGlyph glyph,
                   const QColor& color) {
    drawGlyph(painter, bounds, glyph, color);
}

} // namespace vkui
