// SPDX-License-Identifier: MIT

#include "core/icons/private/VkIconCache_p.h"

#include <QApplication>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QtMath>
#include <QtTest>
#include <limits>
#include <vkui/core/VkFileIcon.h>
#include <vkui/core/VkIcon.h>
#include <vkui/core/VkThemeManager.h>

namespace {

bool hasVisiblePixels(const QImage& image) {
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).alpha() != 0) {
                return true;
            }
        }
    }
    return false;
}

bool hasVisibleBorderPixels(const QImage& image) {
    for (int x = 0; x < image.width(); ++x) {
        if (image.pixelColor(x, 0).alpha() != 0 ||
            image.pixelColor(x, image.height() - 1).alpha() != 0) {
            return true;
        }
    }
    for (int y = 0; y < image.height(); ++y) {
        if (image.pixelColor(0, y).alpha() != 0 ||
            image.pixelColor(image.width() - 1, y).alpha() != 0) {
            return true;
        }
    }
    return false;
}

QColor averageVisibleColor(const QImage& image) {
    quint64 red = 0;
    quint64 green = 0;
    quint64 blue = 0;
    quint64 alpha = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            red += static_cast<quint64>(color.red()) * static_cast<quint64>(color.alpha());
            green += static_cast<quint64>(color.green()) * static_cast<quint64>(color.alpha());
            blue += static_cast<quint64>(color.blue()) * static_cast<quint64>(color.alpha());
            alpha += static_cast<quint64>(color.alpha());
        }
    }
    if (alpha == 0) {
        return {};
    }
    return QColor(static_cast<int>(red / alpha), static_cast<int>(green / alpha),
                  static_cast<int>(blue / alpha));
}

class ApplicationPaletteGuard final {
  public:
    ApplicationPaletteGuard() : original_(QApplication::palette()) {}
    ~ApplicationPaletteGuard() {
        QApplication::setPalette(original_);
    }

  private:
    QPalette original_;
};

class ThemeGuard final {
  public:
    ThemeGuard()
        : appearance_(vkui::VkThemeManager::instance()->appearance()),
          accent_(vkui::VkThemeManager::instance()->accentColor()) {}
    ~ThemeGuard() {
        vkui::VkThemeManager::instance()->setAccentColor(accent_);
        vkui::VkThemeManager::instance()->setAppearance(appearance_);
    }

  private:
    vkui::VkAppearance appearance_;
    vkui::VkAccentColor accent_;
};

} // namespace

class IconTest final : public QObject {
    Q_OBJECT

  private slots:
    void everySymbolHasARenderableResource();
    void rendersModesStatesAndDevicePixelRatios();
    void cacheKeyIncludesEveryRenderDimension();
    void cacheInvalidatesAcrossThemeGenerations();
    void fileIconFontRegistersIdempotently();
    void everyFileGlyphRendersWithoutClipping();
    void fileIconTracksApplicationPaletteAndDevicePixelRatio();
    void fileIconTracksThemeGeneration();
    void fileIconMetricsTrackFontAndDevicePixelRatio();
    void fileGlyphPainterUsesTheSuppliedPalette();
};

void IconTest::everySymbolHasARenderableResource() {
    const QList<vkui::VkSymbol> symbols{
        vkui::VkSymbol::ChevronLeft, vkui::VkSymbol::ChevronRight, vkui::VkSymbol::ChevronUp,
        vkui::VkSymbol::ChevronDown, vkui::VkSymbol::Plus,         vkui::VkSymbol::Minus,
        vkui::VkSymbol::Close,       vkui::VkSymbol::Checkmark,    vkui::VkSymbol::Information,
        vkui::VkSymbol::Warning,     vkui::VkSymbol::Settings,     vkui::VkSymbol::Search,
        vkui::VkSymbol::Folder,      vkui::VkSymbol::Document,     vkui::VkSymbol::Share,
        vkui::VkSymbol::More,        vkui::VkSymbol::ToggleOff,    vkui::VkSymbol::ToggleOn,
        vkui::VkSymbol::Power,       vkui::VkSymbol::Sidebar,      vkui::VkSymbol::Grid,
        vkui::VkSymbol::List,        vkui::VkSymbol::Edit,         vkui::VkSymbol::Bookmark,
        vkui::VkSymbol::Trash,
        vkui::VkSymbol::Download,    vkui::VkSymbol::Upload,       vkui::VkSymbol::Lock,
        vkui::VkSymbol::Eye,         vkui::VkSymbol::Save,         vkui::VkSymbol::Reset,
        vkui::VkSymbol::Duplicate,   vkui::VkSymbol::Image,        vkui::VkSymbol::Background,
        vkui::VkSymbol::Templates,   vkui::VkSymbol::CanvasBackground,
        vkui::VkSymbol::PhotoLibrary, vkui::VkSymbol::Focus,
        vkui::VkSymbol::FocusTarget, vkui::VkSymbol::Rename,       vkui::VkSymbol::Projects,
        vkui::VkSymbol::Remove,      vkui::VkSymbol::Reveal,       vkui::VkSymbol::Clear,
        vkui::VkSymbol::DefaultTemplate, vkui::VkSymbol::UnsavedIndicator,
    };
    for (const vkui::VkSymbol symbol : symbols) {
        const QIcon rendered = vkui::icon(symbol);
        QVERIFY2(!rendered.isNull(), "The icon engine was not created");
        QVERIFY2(!rendered.pixmap(QSize(24, 24)).isNull(), "The SVG resource did not render");
    }
}

void IconTest::rendersModesStatesAndDevicePixelRatios() {
    const QIcon rendered = vkui::icon(vkui::VkSymbol::Information, vkui::VkIconRole::Accent);
    for (const QIcon::Mode mode :
         {QIcon::Normal, QIcon::Disabled, QIcon::Active, QIcon::Selected}) {
        for (const QIcon::State state : {QIcon::Off, QIcon::On}) {
            const QPixmap pixmap = rendered.pixmap(QSize(18, 18), 2.0, mode, state);
            QVERIFY(!pixmap.isNull());
            QCOMPARE(pixmap.devicePixelRatio(), 2.0);
            QCOMPARE(pixmap.size(), QSize(36, 36));
        }
    }
}

void IconTest::cacheKeyIncludesEveryRenderDimension() {
    vkui::VkIconCacheKey base;
    base.symbol = vkui::VkSymbol::Search;
    base.role = vkui::VkIconRole::Primary;
    base.size = QSize(16, 16);
    base.devicePixelRatio = 1024;
    base.mode = QIcon::Normal;
    base.state = QIcon::Off;
    base.themeGeneration = 9;

    auto verifyDifference = [&base](auto mutation) {
        vkui::VkIconCacheKey changed = base;
        mutation(changed);
        QVERIFY(!(changed == base));
        QVERIFY(qHash(changed) != qHash(base));
    };
    verifyDifference([](auto& key) { key.symbol = vkui::VkSymbol::Settings; });
    verifyDifference([](auto& key) { key.role = vkui::VkIconRole::Accent; });
    verifyDifference([](auto& key) { key.size = QSize(17, 16); });
    verifyDifference([](auto& key) { key.devicePixelRatio = 2048; });
    verifyDifference([](auto& key) { key.mode = QIcon::Disabled; });
    verifyDifference([](auto& key) { key.state = QIcon::On; });
    verifyDifference([](auto& key) { key.themeGeneration = 10; });
}

void IconTest::cacheInvalidatesAcrossThemeGenerations() {
    vkui::VkIconCache& cache = vkui::VkIconCache::instance();
    cache.clear();
    vkui::VkIconCacheKey key;
    key.size = QSize(8, 8);
    key.themeGeneration = 100;
    QPixmap source(8, 8);
    source.fill(Qt::red);
    cache.insert(key, source);
    QPixmap result;
    QVERIFY(cache.lookup(key, &result));

    vkui::VkIconCacheKey newer = key;
    newer.themeGeneration = 101;
    QVERIFY(!cache.lookup(newer, &result));
    cache.clear();
}

void IconTest::fileIconFontRegistersIdempotently() {
    QVERIFY(vkui::initializeFileIconFont());
    const QString family = vkui::fileIconFontFamily();
    QVERIFY(!family.isEmpty());
    QVERIFY(QFontDatabase::families().contains(family));

    const QStringList familiesAfterFirstRegistration = QFontDatabase::families();
    QSignalSpy databaseChanges(qGuiApp, &QGuiApplication::fontDatabaseChanged);
    for (int call = 0; call < 32; ++call) {
        QVERIFY(vkui::initializeFileIconFont());
    }
    QCOMPARE(databaseChanges.count(), 0);
    QCOMPARE(QFontDatabase::families(), familiesAfterFirstRegistration);

    const QFont font = vkui::fileIconFont(19);
    QCOMPARE(font.family(), family);
    QCOMPARE(font.pixelSize(), 19);
    QVERIFY(font.fixedPitch());
}

void IconTest::everyFileGlyphRendersWithoutClipping() {
    const QList<vkui::VkFileGlyph> glyphs{
        vkui::VkFileGlyph::FolderClosed,
        vkui::VkFileGlyph::FolderOpen,
        vkui::VkFileGlyph::File,
        vkui::VkFileGlyph::TextFile,
        vkui::VkFileGlyph::CodeFile,
        vkui::VkFileGlyph::ImageFile,
        vkui::VkFileGlyph::PdfFile,
        vkui::VkFileGlyph::ArchiveFile,
        vkui::VkFileGlyph::BookFile,
    };
    for (const vkui::VkFileGlyph glyph : glyphs) {
        const QIcon rendered = vkui::fileIcon(glyph, QColor(31, 93, 220));
        QVERIFY(!rendered.isNull());
        const QImage image = rendered.pixmap(QSize(24, 24)).toImage();
        QVERIFY(hasVisiblePixels(image));
        const QByteArray clippingMessage =
            QByteArrayLiteral("Glyph ") + QByteArray::number(static_cast<int>(glyph)) +
            QByteArrayLiteral(" touched its raster boundary");
        QVERIFY2(!hasVisibleBorderPixels(image), clippingMessage.constData());
    }

    QVERIFY(vkui::fileIcon(static_cast<vkui::VkFileGlyph>(-1)).isNull());
}

void IconTest::fileIconTracksApplicationPaletteAndDevicePixelRatio() {
    ApplicationPaletteGuard restorePalette;
    QPalette redPalette = QApplication::palette();
    redPalette.setColor(QPalette::Active, QPalette::Text, QColor(220, 25, 35));
    QApplication::setPalette(redPalette);

    const QIcon rendered =
        vkui::fileIcon(vkui::VkFileGlyph::TextFile, QPalette::Text, QPalette::Active);
    for (const qreal dpr : {1.25, 1.5, 2.0}) {
        const QPixmap pixmap = rendered.pixmap(QSize(18, 18), dpr);
        QVERIFY(!pixmap.isNull());
        QCOMPARE(pixmap.devicePixelRatio(), dpr);
        QCOMPARE(pixmap.size(), QSize(qCeil(18.0 * dpr), qCeil(18.0 * dpr)));
    }
    const QPixmap redPixmap = rendered.pixmap(QSize(18, 18), 2.0);
    const QColor red = averageVisibleColor(redPixmap.toImage());
    QVERIFY(red.red() > red.blue() * 2);

    QPalette bluePalette = redPalette;
    bluePalette.setColor(QPalette::Active, QPalette::Text, QColor(25, 65, 225));
    QApplication::setPalette(bluePalette);
    const QColor blue = averageVisibleColor(rendered.pixmap(QSize(18, 18), 2.0).toImage());
    QVERIFY(blue.blue() > blue.red() * 2);
}

void IconTest::fileIconTracksThemeGeneration() {
    ThemeGuard restoreTheme;
    auto* manager = vkui::VkThemeManager::instance();
    manager->setAppearance(vkui::VkAppearance::Light);
    manager->setAccentColor(vkui::VkAccentColor::Blue);

    const QIcon rendered =
        vkui::fileIcon(vkui::VkFileGlyph::FolderClosed, vkui::VkIconRole::Accent);
    const QColor blue = averageVisibleColor(rendered.pixmap(QSize(24, 24)).toImage());
    QVERIFY(blue.blue() > blue.red());

    manager->setAccentColor(vkui::VkAccentColor::Red);
    const QColor red = averageVisibleColor(rendered.pixmap(QSize(24, 24)).toImage());
    QVERIFY(red.red() > red.blue());
    QVERIFY(red.rgb() != blue.rgb());
}

void IconTest::fileIconMetricsTrackFontAndDevicePixelRatio() {
    QFont small = vkui::fileIconFont(12);
    QFont large = small;
    large.setPixelSize(24);

    const vkui::VkFileIconMetrics smallOneX = vkui::fileIconMetrics(small);
    const vkui::VkFileIconMetrics smallTwoX = vkui::fileIconMetrics(small, 2.0);
    const vkui::VkFileIconMetrics largeOneX = vkui::fileIconMetrics(large);
    QVERIFY(smallOneX.glyphPixelSize > 0);
    QVERIFY(smallOneX.glyphSlotSize.width() > 0);
    QVERIFY(smallOneX.glyphSlotSize.height() > 0);
    QVERIFY(smallOneX.textGap > 0);
    QVERIFY(smallOneX.rowHeight >= smallOneX.glyphSlotSize.height());
    QCOMPARE(smallTwoX.glyphSlotSize, smallOneX.glyphSlotSize);
    QCOMPARE(smallTwoX.devicePixelGlyphSlotSize, smallOneX.glyphSlotSize * 2);
    QCOMPARE(smallTwoX.devicePixelRatio, 2.0);
    QVERIFY(largeOneX.glyphPixelSize > smallOneX.glyphPixelSize);
    QVERIFY(largeOneX.glyphSlotSize.height() > smallOneX.glyphSlotSize.height());
    QVERIFY(largeOneX.rowHeight > smallOneX.rowHeight);

    const vkui::VkFileIconMetrics invalidDpr =
        vkui::fileIconMetrics(small, std::numeric_limits<qreal>::quiet_NaN());
    QCOMPARE(invalidDpr.devicePixelRatio, 1.0);
    QCOMPARE(invalidDpr.devicePixelGlyphSlotSize, invalidDpr.glyphSlotSize);
}

void IconTest::fileGlyphPainterUsesTheSuppliedPalette() {
    QPalette palette;
    palette.setColor(QPalette::Active, QPalette::Highlight, QColor(40, 190, 75));
    QImage image(QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    vkui::drawFileGlyph(painter, image.rect(), vkui::VkFileGlyph::FolderOpen, palette,
                        QPalette::Highlight, QPalette::Active);
    painter.end();

    QVERIFY(hasVisiblePixels(image));
    const QColor rendered = averageVisibleColor(image);
    QVERIFY(rendered.green() > rendered.red() * 2);
    QVERIFY(rendered.green() > rendered.blue() * 2);
}

QTEST_MAIN(IconTest)
#include "tst_icons.moc"
