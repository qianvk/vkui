// SPDX-License-Identifier: MIT

#include "core/icons/private/VkIconCache_p.h"
#include "core/icons/private/VkSvgIconSourceCache_p.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QtMath>
#include <QtTest>
#include <algorithm>
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
    void explicitSvgIconUsesCallerColors();
    void explicitSvgIconPreservesCallerAlpha();
    void sourceAndMetadataAreSharedPerSymbol();
    void chosenSymbolsUseCanonicalCanvas();
    void cacheKeyIncludesEveryRenderDimension();
    void cacheInvalidatesAcrossColorGenerations();
    void everyFileSymbolRendersWithoutClipping();
    void fileIconTracksApplicationPaletteAndDevicePixelRatio();
    void fileIconTracksColorGeneration();
    void fileIconMetricsTrackFontAndDevicePixelRatio();
    void fileSymbolResolvesFromPathWithoutFilesystemAccess();
    void fileIconUsesTheSuppliedPalette();
};

void IconTest::fileSymbolResolvesFromPathWithoutFilesystemAccess() {
    QCOMPARE(vkui::fileSymbolForPath(u"notes/chapter.TXT"), vkui::VkSymbol::FileText);
    QCOMPARE(vkui::fileSymbolForPath(u"notes/readme.md"), vkui::VkSymbol::FileMarkdown);
    QCOMPARE(vkui::fileSymbolForPath(u"notes/spec.MARKDOWN"), vkui::VkSymbol::FileMarkdown);
    QCOMPARE(vkui::fileSymbolForPath(u"src/main.cpp"), vkui::VkSymbol::FileCode);
    QCOMPARE(vkui::fileSymbolForPath(u"covers/hero.webp"), vkui::VkSymbol::FileImage);
    QCOMPARE(vkui::fileSymbolForPath(u"library/book.epub"), vkui::VkSymbol::FileBook);
    QCOMPARE(vkui::fileSymbolForPath(u"paper.pdf"), vkui::VkSymbol::FilePdf);
    QCOMPARE(vkui::fileSymbolForPath(u"archive.tar.gz"), vkui::VkSymbol::FileArchive);
    QCOMPARE(vkui::fileSymbolForPath(u"folder.with.dot/file"), vkui::VkSymbol::FileGeneric);
}

void IconTest::everySymbolHasARenderableResource() {
    for (int value = 0; value < static_cast<int>(vkui::VkSymbol::Count); ++value) {
        const auto symbol = static_cast<vkui::VkSymbol>(value);
        const QIcon rendered = vkui::icon(symbol);
        QVERIFY2(!rendered.isNull(), "The icon engine was not created");
        const QImage image = rendered.pixmap(QSize(24, 24)).toImage();
        QVERIFY2(!image.isNull(), "The SVG resource did not render");
        int visiblePixels = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                visiblePixels += image.pixelColor(x, y).alpha() > 16 ? 1 : 0;
            }
        }
        QVERIFY2(visiblePixels >= 12, "The SVG resource rendered no visible symbol");
    }
}

void IconTest::sourceAndMetadataAreSharedPerSymbol() {
    const auto first = vkui::VkSvgIconSourceCache::source(vkui::VkSymbol::Search);
    const auto second = vkui::VkSvgIconSourceCache::source(vkui::VkSymbol::Search);
    QVERIFY(first);
    QCOMPARE(first.get(), second.get());
    QVERIFY(!first->source.isEmpty());
    QVERIFY(!first->intrinsicSize.isEmpty());
    QVERIFY(!vkui::VkSvgIconSourceCache::source(vkui::VkSymbol::Count));
}

void IconTest::chosenSymbolsUseCanonicalCanvas() {
    const QList<vkui::VkSymbol> chosen{
        vkui::VkSymbol::GearFilled,       vkui::VkSymbol::FileFolderClosed,
        vkui::VkSymbol::FileFolderOpen,   vkui::VkSymbol::FileGeneric,
        vkui::VkSymbol::CloudFilled,      vkui::VkSymbol::CloseCircleFilled,
    };
    for (const vkui::VkSymbol symbol : chosen) {
        const auto source = vkui::VkSvgIconSourceCache::source(symbol);
        QVERIFY(source);
        QCOMPARE(source->intrinsicSize, QSize(24, 24));
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

void IconTest::explicitSvgIconUsesCallerColors() {
    const QIcon darkSurfaceIcon = vkui::icon(vkui::VkSymbol::List, QColor(Qt::white));
    const QColor lightInk =
        averageVisibleColor(darkSurfaceIcon.pixmap(QSize(24, 24), 2.0).toImage());
    QVERIFY(lightInk.lightnessF() > 0.8);

    const QIcon lightSurfaceIcon = vkui::icon(vkui::VkSymbol::Background, QColor(Qt::black));
    const QColor darkInk =
        averageVisibleColor(lightSurfaceIcon.pixmap(QSize(24, 24), 2.0).toImage());
    QVERIFY(darkInk.lightnessF() < 0.2);
}

void IconTest::explicitSvgIconPreservesCallerAlpha() {
    const QIcon translucent =
        vkui::icon(vkui::VkSymbol::GearFilled, QColor(255, 40, 20, 128));
    const QImage image = translucent.pixmap(QSize(32, 32), 2.0).toImage();
    QVERIFY(!image.isNull());

    int maximumAlpha = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            maximumAlpha = std::max(maximumAlpha, image.pixelColor(x, y).alpha());
        }
    }
    QVERIFY(maximumAlpha >= 126);
    QVERIFY(maximumAlpha <= 128);
}

void IconTest::cacheKeyIncludesEveryRenderDimension() {
    vkui::VkIconCacheKey base;
    base.symbol = vkui::VkSymbol::Search;
    base.role = vkui::VkIconRole::Primary;
    base.size = QSize(16, 16);
    base.devicePixelRatio = 1024;
    base.mode = QIcon::Normal;
    base.state = QIcon::Off;
    base.colorGeneration = 9;

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
    verifyDifference([](auto& key) { key.colorGeneration = 10; });
    verifyDifference([](auto& key) { key.colorIdentity = 42; });
}

void IconTest::cacheInvalidatesAcrossColorGenerations() {
    vkui::VkIconCache& cache = vkui::VkIconCache::instance();
    cache.clear();
    vkui::VkIconCacheKey key;
    key.size = QSize(8, 8);
    key.colorGeneration = 100;
    QPixmap source(8, 8);
    source.fill(Qt::red);
    cache.insert(key, source);
    QPixmap result;
    QVERIFY(cache.lookup(key, &result));

    vkui::VkIconCacheKey newer = key;
    newer.colorGeneration = 101;
    QVERIFY(!cache.lookup(newer, &result));

    // A bounded LRU can retain multiple valid theme/color generations. This
    // prevents semantic and explicit-palette icons from continuously flushing
    // one another when they are painted in the same frame.
    vkui::VkIconCacheKey explicitColor = key;
    explicitColor.colorGeneration = 0;
    explicitColor.colorIdentity = 0xffeeddcc;
    cache.insert(explicitColor, source);
    QVERIFY(cache.lookup(key, &result));
    QVERIFY(cache.lookup(explicitColor, &result));
    cache.clear();
}

void IconTest::everyFileSymbolRendersWithoutClipping() {
    const QList<vkui::VkSymbol> symbols{
        vkui::VkSymbol::FileFolderClosed, vkui::VkSymbol::FileFolderOpen,
        vkui::VkSymbol::FileGeneric,      vkui::VkSymbol::FileText,
        vkui::VkSymbol::FileMarkdown,     vkui::VkSymbol::FileCode,
        vkui::VkSymbol::FileImage,        vkui::VkSymbol::FilePdf,
        vkui::VkSymbol::FileArchive,      vkui::VkSymbol::FileBook,
    };
    for (const vkui::VkSymbol symbol : symbols) {
        const QIcon rendered = vkui::icon(symbol, QColor(31, 93, 220));
        QVERIFY(!rendered.isNull());
        const QImage image = rendered.pixmap(QSize(24, 24)).toImage();
        QVERIFY(hasVisiblePixels(image));
        const QByteArray clippingMessage = QByteArrayLiteral("Symbol ") +
                                           QByteArray::number(static_cast<int>(symbol)) +
                                           QByteArrayLiteral(" touched its raster boundary");
        QVERIFY2(!hasVisibleBorderPixels(image), clippingMessage.constData());
    }
    QVERIFY(vkui::icon(vkui::VkSymbol::Count).isNull());
}

void IconTest::fileIconTracksApplicationPaletteAndDevicePixelRatio() {
    ApplicationPaletteGuard restorePalette;
    QPalette redPalette = QApplication::palette();
    redPalette.setColor(QPalette::Active, QPalette::Text, QColor(220, 25, 35));
    QApplication::setPalette(redPalette);

    const QIcon rendered = vkui::icon(vkui::VkSymbol::FileText, QPalette::Text, QPalette::Active);
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

void IconTest::fileIconTracksColorGeneration() {
    ThemeGuard restoreTheme;
    auto* manager = vkui::VkThemeManager::instance();
    manager->setAppearance(vkui::VkAppearance::Light);
    manager->setAccentColor(vkui::VkAccentColor::Blue);

    const QIcon rendered = vkui::icon(vkui::VkSymbol::FileFolderClosed, vkui::VkIconRole::Accent);
    const QColor blue = averageVisibleColor(rendered.pixmap(QSize(24, 24)).toImage());
    QVERIFY(blue.blue() > blue.red());

    manager->setAccentColor(vkui::VkAccentColor::Red);
    const QColor red = averageVisibleColor(rendered.pixmap(QSize(24, 24)).toImage());
    QVERIFY(red.red() > red.blue());
    QVERIFY(red.rgb() != blue.rgb());
}

void IconTest::fileIconMetricsTrackFontAndDevicePixelRatio() {
    QFont small = QApplication::font();
    small.setPixelSize(12);
    QFont large = small;
    large.setPixelSize(24);

    const vkui::VkFileIconMetrics smallMetrics = vkui::fileIconMetrics(small);
    const vkui::VkFileIconMetrics largeMetrics = vkui::fileIconMetrics(large);
    QVERIFY(smallMetrics.iconSize.width() > 0);
    QCOMPARE(smallMetrics.iconSize.width(), smallMetrics.iconSize.height());
    QVERIFY(smallMetrics.textGap > 0);
    QVERIFY(smallMetrics.rowHeight >= smallMetrics.iconSize.height());
    QVERIFY(largeMetrics.iconSize.height() > smallMetrics.iconSize.height());
    QVERIFY(largeMetrics.rowHeight > smallMetrics.rowHeight);
}

void IconTest::fileIconUsesTheSuppliedPalette() {
    QPalette palette;
    palette.setColor(QPalette::Active, QPalette::Highlight, QColor(40, 190, 75));
    QImage image(QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    vkui::icon(vkui::VkSymbol::FileFolderOpen, palette.color(QPalette::Active, QPalette::Highlight))
        .paint(&painter, image.rect());
    painter.end();

    QVERIFY(hasVisiblePixels(image));
    const QColor rendered = averageVisibleColor(image);
    QVERIFY(rendered.green() > rendered.red() * 2);
    QVERIFY(rendered.green() > rendered.blue() * 2);
}

QTEST_MAIN(IconTest)
#include "tst_icons.moc"
