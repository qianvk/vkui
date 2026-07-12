// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QPalette>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>
#include <vkui/core/VkThemeManager.h>

class ThemeTest final : public QObject {
    Q_OBJECT

  private slots:
    void resolvedThemeIsComplete();
    void explicitAppearanceChangesGeneration();
    void appearanceSignalsArePrecise();
    void accentColorsAreDistinctAndGenerationSafe();
    void resolvedPaletteIsApplied();
};

void ThemeTest::resolvedThemeIsComplete() {
    const vkui::VkTheme& theme = vkui::VkThemeManager::instance()->theme();
    const auto& colors = theme.colors();
    const QColor* const semanticColors[]{
        &colors.windowBackground,
        &colors.contentBackground,
        &colors.elevatedBackground,
        &colors.popoverBackground,
        &colors.controlFill,
        &colors.controlFillHovered,
        &colors.controlFillPressed,
        &colors.controlFillDisabled,
        &colors.separator,
        &colors.border,
        &colors.borderStrong,
        &colors.textPrimary,
        &colors.textSecondary,
        &colors.textTertiary,
        &colors.textDisabled,
        &colors.accent,
        &colors.accentHovered,
        &colors.accentPressed,
        &colors.destructive,
        &colors.warning,
        &colors.success,
        &colors.focusRing,
        &colors.shadow,
        &colors.symbolPrimary,
        &colors.symbolSecondary,
        &colors.symbolDisabled,
    };
    for (const QColor* color : semanticColors) {
        QVERIFY(color->isValid());
    }

    const auto& metrics = theme.metrics();
    QVERIFY(metrics.spacing2 > 0.0);
    QVERIFY(metrics.spacing2 < metrics.spacing24);
    QVERIFY(metrics.controlHeightSmall < metrics.controlHeightRegular);
    QVERIFY(metrics.controlHeightRegular < metrics.controlHeightLarge);
    QCOMPARE(metrics.fixedControlExtentSmall, 14.0);
    QCOMPARE(metrics.fixedControlExtentRegular, 18.0);
    QCOMPARE(metrics.fixedControlExtentLarge, 22.0);
    QVERIFY(metrics.switchThumbDiameter <= metrics.switchTrackHeight);
    QCOMPARE(metrics.switchTrackHeight, 18.0);
    QCOMPARE(metrics.switchThumbDiameter, 14.0);
    QVERIFY(metrics.popoverArrowWidth > metrics.popoverArrowDepth);
    QVERIFY(metrics.popoverCornerRadius > 0.0);
    QVERIFY(metrics.popoverScreenMargin > 0.0);
    QVERIFY(metrics.popoverShadowRadius > 0.0);
#if defined(Q_OS_MACOS)
    QCOMPARE(metrics.cornerRadiusLarge, 16.0);
#else
    QCOMPARE(metrics.cornerRadiusLarge, 10.0);
#endif

    const auto& type = theme.typography();
    QVERIFY(!type.body.family().isEmpty());
    QVERIFY(type.caption.pointSizeF() > 0.0);
    QVERIFY(type.bodyEmphasized.weight() >= type.body.weight());
    QVERIFY(type.largeTitle.pointSizeF() > type.body.pointSizeF());
    QVERIFY(theme.generation() > 0);
    QVERIFY(theme.effectiveAppearance() != vkui::VkAppearance::Auto);
}

void ThemeTest::accentColorsAreDistinctAndGenerationSafe() {
    auto* manager = vkui::VkThemeManager::instance();
    const vkui::VkAppearance originalAppearance = manager->appearance();
    const vkui::VkAccentColor originalAccent = manager->accentColor();
    manager->setAppearance(vkui::VkAppearance::Light);
    manager->setAccentColor(vkui::VkAccentColor::Blue);

    QSignalSpy accentSpy(manager, &vkui::VkThemeManager::accentColorChanged);
    QSignalSpy themeSpy(manager, &vkui::VkThemeManager::themeChanged);
    QSet<QRgb> resolvedColors;
    resolvedColors.insert(manager->theme().colors().accent.rgba());
    quint64 generation = manager->theme().generation();

    const QList<vkui::VkAccentColor> accents{
        vkui::VkAccentColor::Purple,   vkui::VkAccentColor::Pink,   vkui::VkAccentColor::Red,
        vkui::VkAccentColor::Orange,   vkui::VkAccentColor::Yellow, vkui::VkAccentColor::Green,
        vkui::VkAccentColor::Graphite,
    };
    for (const vkui::VkAccentColor accent : accents) {
        manager->setAccentColor(accent);
        QCOMPARE(manager->accentColor(), accent);
        QVERIFY(manager->theme().generation() > generation);
        generation = manager->theme().generation();
        resolvedColors.insert(manager->theme().colors().accent.rgba());
        QCOMPARE(qApp->palette().color(QPalette::Highlight), manager->theme().colors().accent);
    }
    QCOMPARE(resolvedColors.size(), 8);
    QCOMPARE(accentSpy.count(), 7);
    QCOMPARE(themeSpy.count(), 7);

    manager->setAccentColor(vkui::VkAccentColor::Graphite);
    QCOMPARE(accentSpy.count(), 7);
    QCOMPARE(themeSpy.count(), 7);
    manager->setAccentColor(originalAccent);
    manager->setAppearance(originalAppearance);
}

void ThemeTest::explicitAppearanceChangesGeneration() {
    auto* manager = vkui::VkThemeManager::instance();
    const vkui::VkAppearance original = manager->appearance();
    manager->setAppearance(vkui::VkAppearance::Light);
    const quint64 lightGeneration = manager->theme().generation();
    manager->setAppearance(vkui::VkAppearance::Dark);
    QVERIFY(manager->theme().generation() > lightGeneration);
    QCOMPARE(manager->effectiveAppearance(), vkui::VkAppearance::Dark);
    manager->setAppearance(original);
}

void ThemeTest::appearanceSignalsArePrecise() {
    auto* manager = vkui::VkThemeManager::instance();
    const vkui::VkAppearance original = manager->appearance();
    manager->setAppearance(vkui::VkAppearance::Light);
    QSignalSpy appearanceSpy(manager, &vkui::VkThemeManager::appearanceChanged);
    QSignalSpy effectiveSpy(manager, &vkui::VkThemeManager::effectiveAppearanceChanged);
    QSignalSpy themeSpy(manager, &vkui::VkThemeManager::themeChanged);

    manager->setAppearance(vkui::VkAppearance::Dark);
    QCOMPARE(appearanceSpy.count(), 1);
    QCOMPARE(effectiveSpy.count(), 1);
    QCOMPARE(themeSpy.count(), 1);
    manager->setAppearance(vkui::VkAppearance::Dark);
    QCOMPARE(appearanceSpy.count(), 1);
    QCOMPARE(themeSpy.count(), 1);
    manager->setAppearance(original);
}

void ThemeTest::resolvedPaletteIsApplied() {
    auto* manager = vkui::VkThemeManager::instance();
    const vkui::VkAppearance original = manager->appearance();
    manager->setAppearance(vkui::VkAppearance::Dark);
    QCOMPARE(qApp->palette().color(QPalette::Window), manager->theme().colors().windowBackground);
    QCOMPARE(qApp->palette().color(QPalette::WindowText), manager->theme().colors().textPrimary);
    manager->setAppearance(original);
}

QTEST_MAIN(ThemeTest)
#include "tst_theme.moc"
