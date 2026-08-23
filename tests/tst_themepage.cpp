// SPDX-License-Identifier: MIT

#include "ThemePage.h"

#include <QtTest/QTest>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/controls/VSlider.h>

class ThemePageTest final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void textScaleSliderUsesCanonicalRangeAndUpdatesLive();

  private:
    qreal originalTextScale_ = vkui::VkDefaultTextScale;
};

void ThemePageTest::initTestCase() {
    originalTextScale_ = vkui::VkThemeManager::instance()->textScale();
}

void ThemePageTest::cleanupTestCase() {
    vkui::VkThemeManager::instance()->setTextScale(originalTextScale_);
}

void ThemePageTest::textScaleSliderUsesCanonicalRangeAndUpdatesLive() {
    vkui::VkThemeManager::instance()->resetTextScale();
    ThemePage page;
    auto* slider = page.findChild<vkui::VSlider*>(
        QStringLiteral("interfaceTextScaleSlider"));
    QVERIFY(slider != nullptr);

    QCOMPARE(slider->orientation(), Qt::Horizontal);
    QCOMPARE(slider->minimum(), 80);
    QCOMPARE(slider->maximum(), 160);
    QCOMPARE(slider->singleStep(), 5);
    QCOMPARE(slider->pageStep(), 10);
    QCOMPARE(slider->tickInterval(), 10);
    QVERIFY(slider->hasTracking());

    // Programmatic values can bypass QSlider's step. The manager remains authoritative and
    // immediately snaps both the resolved theme and the presentation back to a canonical step.
    slider->setValue(123);
    QCOMPARE(vkui::VkThemeManager::instance()->textScale(), 1.25);
    QCOMPARE(slider->value(), 125);
    slider->setValue(124);
    QCOMPARE(vkui::VkThemeManager::instance()->textScale(), 1.25);
    QCOMPARE(slider->value(), 125);

    vkui::VkThemeManager::instance()->setTextScale(1.40);
    QCOMPARE(slider->value(), 140);
}

QTEST_MAIN(ThemePageTest)

#include "tst_themepage.moc"
