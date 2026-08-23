// SPDX-License-Identifier: MIT

#include "../examples/gallery/GalleryPreferencesWindow.h"

#include <QDialog>
#include <QGuiApplication>
#include <QPointer>
#include <QWidget>
#include <QtTest>
#include <type_traits>

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

QTEST_MAIN(GalleryPreferencesTest)
#include "tst_gallerypreferences.moc"
