// SPDX-License-Identifier: MIT

#include "GalleryWindow.h"

#include <QPointer>
#include <QtTest>
#include <vkui/Widgets.h>
#include <vkui/widgets/effects/VLiquidGlass.h>

class GalleryWindowTest final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void languageActivationDefersUiRebuild();
};

void GalleryWindowTest::initTestCase() {
    vkui::installVkUi(*qApp);
}

void GalleryWindowTest::languageActivationDefersUiRebuild() {
    GalleryWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto* appearanceSurface =
        window.findChild<vkui::VLiquidGlassSurface*>(QStringLiteral("galleryAppearanceGlass"));
    auto* languageSurface =
        window.findChild<vkui::VLiquidGlassSurface*>(QStringLiteral("galleryLanguageGlass"));
    QVERIFY(appearanceSurface != nullptr);
    QVERIFY(languageSurface != nullptr);
    QCOMPARE(appearanceSurface->glassStyle().opticalEdgeIntensity, 0.0);
    QCOMPARE(languageSurface->glassStyle().opticalEdgeIntensity, 0.0);
    QVERIFY(!appearanceSurface->glassStyle().drawsBorder);
    QVERIFY(!languageSurface->glassStyle().drawsBorder);

    QPointer<vkui::VCombobox> original =
        window.findChild<vkui::VCombobox*>(QStringLiteral("galleryLanguageBox"));
    QVERIFY(original);
    constexpr int SimplifiedChinese = 2;
    const int targetIndex = original->findData(SimplifiedChinese);
    QVERIFY(targetIndex >= 0);

    QVERIFY(QMetaObject::invokeMethod(original, "activated", Qt::DirectConnection,
                                      Q_ARG(int, targetIndex)));
    // The signal sender must survive until QComboBox finishes delivering activated().
    QVERIFY(original);

    QTRY_VERIFY(original.isNull());
    auto* rebuilt = window.findChild<vkui::VCombobox*>(QStringLiteral("galleryLanguageBox"));
    QVERIFY(rebuilt != nullptr);
    QCOMPARE(rebuilt->currentData().toInt(), SimplifiedChinese);
}

QTEST_MAIN(GalleryWindowTest)

#include "tst_gallerywindow.moc"
