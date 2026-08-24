// SPDX-License-Identifier: MIT

#include "../examples/gallery/GalleryPreferencesWindow.h"

#include <QDialog>
#include <QGuiApplication>
#include <QLabel>
#include <QPointer>
#include <QSlider>
#include <QWidget>
#include <QtTest>
#include <type_traits>
#include <vkui/core/VkTextSize.h>
#include <vkui/core/VkThemeManager.h>

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
int macVisibleSystemButtons(WId nativeViewId);
bool macPerformNativeClose(WId nativeViewId);
#endif

static_assert(std::is_base_of_v<QWidget, GalleryPreferencesWindow>);
static_assert(!std::is_base_of_v<QDialog, GalleryPreferencesWindow>);

class GalleryPreferencesTest final : public QObject {
    Q_OBJECT

  private slots:
    void closeDestroysTheSingleLiveWindow();
    void textSizeControlUsesTheApplicationTheme();
};

void GalleryPreferencesTest::closeDestroysTheSingleLiveWindow() {
    QWidget host;
    host.setGeometry(120, 90, 1000, 800);
    host.show();

    QPointer<GalleryPreferencesWindow> preferences = new GalleryPreferencesWindow;
    QVERIFY(preferences->testAttribute(Qt::WA_DeleteOnClose));
    QVERIFY(!preferences->testAttribute(Qt::WA_QuitOnClose));
    QCOMPARE(preferences->windowModality(), Qt::NonModal);
    auto* titleBar =
        preferences->findChild<QWidget*>(QStringLiteral("GalleryPreferencesTitleBar"));
    QVERIFY(titleBar != nullptr);
    QVERIFY(!titleBar->autoFillBackground());
    QVERIFY(!titleBar->testAttribute(Qt::WA_StyledBackground));

    preferences->showForHost(&host);
    QTRY_VERIFY(preferences->isVisible());
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (QGuiApplication::platformName().startsWith(QStringLiteral("cocoa"), Qt::CaseInsensitive)) {
        const WId nativeViewId = preferences->winId();
        QTRY_COMPARE(macVisibleSystemButtons(nativeViewId), 0x01);
        QVERIFY(macPerformNativeClose(nativeViewId));
    } else {
        preferences->close();
    }
#else
    preferences->close();
#endif
    QTRY_VERIFY(preferences.isNull());
}

void GalleryPreferencesTest::textSizeControlUsesTheApplicationTheme() {
    auto* manager = vkui::VkThemeManager::instance();
    const int originalLevel = manager->textSizeLevel();
    manager->resetTextSizeLevel();

    GalleryPreferencesWindow preferences;
    auto* slider = preferences.findChild<QSlider*>(QStringLiteral("preferencesTextSizeSlider"));
    auto* value = preferences.findChild<QLabel*>(QStringLiteral("preferencesTextSizeValue"));
    QVERIFY(slider != nullptr);
    QVERIFY(value != nullptr);
    QCOMPARE(slider->minimum(), vkui::VkMinimumTextSizeLevel);
    QCOMPARE(slider->maximum(), vkui::VkMaximumTextSizeLevel);
    QCOMPARE(slider->singleStep(), 1);
    QCOMPARE(slider->tickPosition(), QSlider::TicksBelow);

    slider->setValue(7);
    QCOMPARE(manager->textSizeLevel(), 7);
    QVERIFY(value->text().contains(QString::number(7)));

    manager->setTextSizeLevel(9);
    QCOMPARE(slider->value(), 9);
    QVERIFY(value->text().contains(QString::number(9)));
    manager->setTextSizeLevel(originalLevel);
}

QTEST_MAIN(GalleryPreferencesTest)
#include "tst_gallerypreferences.moc"
