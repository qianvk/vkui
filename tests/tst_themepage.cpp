// SPDX-License-Identifier: MIT

#include "ThemePage.h"

#include <QAbstractButton>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QStyleOptionSlider>
#include <QtTest/QTest>
#include <cmath>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VCombobox.h>
#include <vkui/widgets/controls/VSlider.h>
#include <vkui/widgets/controls/VSwitch.h>
#include <vkui/widgets/effects/VLiquidGlass.h>
#include <vkui/widgets/style/VStyle.h>

class ThemePageTest final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void textSizeSliderMatchesThingsLevelsAndUpdatesLive();
    void liquidGlassControlAndPreviewUpdateLive();

  private:
    int originalTextSizeLevel_ = vkui::VkDefaultTextSizeLevel;
    bool originalLiquidGlassEnabled_ = true;
};

void ThemePageTest::initTestCase() {
    vkui::installVkUi(*qApp);
    originalTextSizeLevel_ = vkui::VkThemeManager::instance()->textSizeLevel();
    originalLiquidGlassEnabled_ = vkui::VkThemeManager::instance()->liquidGlassEnabled();
}

void ThemePageTest::cleanupTestCase() {
    vkui::VkThemeManager::instance()->setTextSizeLevel(originalTextSizeLevel_);
    vkui::VkThemeManager::instance()->setLiquidGlassEnabled(originalLiquidGlassEnabled_);
}

void ThemePageTest::liquidGlassControlAndPreviewUpdateLive() {
    auto* manager = vkui::VkThemeManager::instance();
    manager->setLiquidGlassEnabled(true);
    ThemePage page;
    page.resize(760, 1100);
    page.show();
    QCoreApplication::processEvents();

    auto* toggle = page.findChild<vkui::VSwitch*>(QStringLiteral("liquidGlassEnabledSwitch"));
    auto* preview = page.findChild<QWidget*>(QStringLiteral("liquidGlassPreview"));
    QVERIFY(toggle != nullptr);
    QVERIFY(preview != nullptr);
    QCOMPARE(preview->findChildren<vkui::VLiquidGlassSurface*>().size(), 2);
    QVERIFY(toggle->isChecked());

    QImage enabledPreview(preview->size(), QImage::Format_ARGB32_Premultiplied);
    enabledPreview.fill(Qt::transparent);
    preview->render(&enabledPreview);

    toggle->click();
    QCOMPARE(manager->liquidGlassEnabled(), false);
    QCoreApplication::processEvents();
    QImage disabledPreview(preview->size(), QImage::Format_ARGB32_Premultiplied);
    disabledPreview.fill(Qt::transparent);
    preview->render(&disabledPreview);
    QVERIFY(enabledPreview != disabledPreview);
    manager->setLiquidGlassEnabled(true);
    QCOMPARE(toggle->isChecked(), true);
}

void ThemePageTest::textSizeSliderMatchesThingsLevelsAndUpdatesLive() {
    vkui::VkThemeManager::instance()->resetTextSizeLevel();
    ThemePage page;
    page.resize(760, 980);
    page.show();
    QCoreApplication::processEvents();
    auto* slider = page.findChild<vkui::VSlider*>(QStringLiteral("interfaceTextSizeSlider"));
    QVERIFY(slider != nullptr);

    QCOMPARE(slider->orientation(), Qt::Horizontal);
    QCOMPARE(slider->minimum(), 1);
    QCOMPARE(slider->maximum(), 12);
    QCOMPARE(slider->value(), 3);
    QCOMPARE(slider->singleStep(), 1);
    QCOMPARE(slider->pageStep(), 1);
    QCOMPARE(slider->tickInterval(), 1);
    QCOMPARE(slider->tickPosition(), QSlider::TicksBelow);
    QVERIFY(slider->hasTracking());

    const auto radios = page.findChildren<QRadioButton*>();
    auto* previewCombo = page.findChild<vkui::VCombobox*>();
    const auto groups = page.findChildren<QGroupBox*>();
    QVERIFY(!radios.isEmpty());
    QVERIFY(previewCombo != nullptr);
    QVERIFY(!groups.isEmpty());
    const int defaultRadioFontHeight = radios.constFirst()->fontMetrics().height();
    const int defaultComboFontHeight = previewCombo->fontMetrics().height();
    const int defaultGroupFontHeight = groups.constFirst()->fontMetrics().height();

    slider->setValue(8);
    QCOMPARE(vkui::VkThemeManager::instance()->textSizeLevel(), 8);
    QCoreApplication::processEvents();
    for (QRadioButton* radio : radios) {
        QVERIFY(radio->fontMetrics().height() > defaultRadioFontHeight);
    }
    QVERIFY(previewCombo->fontMetrics().height() > defaultComboFontHeight);
    for (QGroupBox* group : groups) {
        QVERIFY(group->fontMetrics().height() > defaultGroupFontHeight);
    }

    vkui::VkThemeManager::instance()->setTextSizeLevel(11);
    QCOMPARE(slider->value(), 11);

    auto* defaultButton = page.findChild<QAbstractButton*>(QStringLiteral("defaultTextSizeButton"));
    QVERIFY(defaultButton != nullptr);
    auto* smallerLabel = page.findChild<QLabel*>(QStringLiteral("textSizeSmallerLabel"));
    auto* largerLabel = page.findChild<QLabel*>(QStringLiteral("textSizeLargerLabel"));
    QVERIFY(smallerLabel != nullptr);
    QVERIFY(largerLabel != nullptr);
    QVERIFY(largerLabel->fontMetrics().height() > smallerLabel->fontMetrics().height());

    defaultButton->click();
    QCOMPARE(vkui::VkThemeManager::instance()->textSizeLevel(), 3);
    QCOMPARE(slider->value(), 3);
    QCoreApplication::processEvents();

    QStyleOptionSlider option;
    option.initFrom(slider);
    option.orientation = slider->orientation();
    option.minimum = slider->minimum();
    option.maximum = slider->maximum();
    option.sliderPosition = slider->sliderPosition();
    option.sliderValue = slider->value();
    option.singleStep = slider->singleStep();
    option.pageStep = slider->pageStep();
    option.tickInterval = slider->tickInterval();
    option.tickPosition = slider->tickPosition();
    option.upsideDown = slider->invertedAppearance();
    if (slider->layoutDirection() == Qt::RightToLeft) {
        option.upsideDown = !option.upsideDown;
    }
    const QRect handle = slider->style()->subControlRect(QStyle::CC_Slider, &option,
                                                         QStyle::SC_SliderHandle, slider);
    const int handleCenter = slider->mapTo(&page, handle.center()).x();
    const int defaultCenter = defaultButton->mapTo(&page, defaultButton->rect().center()).x();
    QVERIFY2(std::abs(handleCenter - defaultCenter) <= 1,
             qPrintable(QStringLiteral("handle center %1, Default center %2")
                            .arg(handleCenter)
                            .arg(defaultCenter)));
}

QTEST_MAIN(ThemePageTest)

#include "tst_themepage.moc"
