// SPDX-License-Identifier: MIT

#include "core/icons/private/VkIconCache_p.h"

#include <QPixmap>
#include <QtTest>
#include <vkui/core/VkIcon.h>
#include <vkui/core/VkThemeManager.h>

class IconTest final : public QObject {
    Q_OBJECT

  private slots:
    void everySymbolHasARenderableResource();
    void rendersModesStatesAndDevicePixelRatios();
    void cacheKeyIncludesEveryRenderDimension();
    void cacheInvalidatesAcrossThemeGenerations();
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
        vkui::VkSymbol::List,        vkui::VkSymbol::Edit,         vkui::VkSymbol::Trash,
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

QTEST_MAIN(IconTest)
#include "tst_icons.moc"
