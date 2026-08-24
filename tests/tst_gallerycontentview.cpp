// SPDX-License-Identifier: MIT

#include "../examples/gallery/GalleryContentView.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QtTest>
#include <vkui/widgets/effects/VLiquidGlass.h>

class GalleryContentViewTest final : public QObject {
    Q_OBJECT

  private slots:
    void scrollableInsetMovesBehindTransparentTitleBar();
};

void GalleryContentViewTest::scrollableInsetMovesBehindTransparentTitleBar() {
    GalleryContentView view;
    view.resize(360, 240);

    auto* page = new QWidget;
    page->setMinimumHeight(640);
    page->setBackgroundRole(QPalette::Window);
    page->setAutoFillBackground(true);
    QPalette pagePalette = page->palette();
    const QColor pageColor(217, 72, 61);
    pagePalette.setColor(QPalette::Window, pageColor);
    page->setPalette(pagePalette);
    view.addPage(page);
    view.titleBarLayout()->addStretch();
    auto* titleBarControl = new QPushButton(QStringLiteral("Control"), &view);
    view.titleBarLayout()->addWidget(titleBarControl);
    auto* glass = new vkui::VLiquidGlassSurface(&view);
    glass->setGeometry(90, 8, 120, 40);
    vkui::VLiquidGlassStyle exactStyle;
    exactStyle.cornerRadius = 0.0;
    exactStyle.blurRadius = 0.0;
    exactStyle.refractionHeight = 0.0;
    exactStyle.refractionAmount = 0.0;
    exactStyle.chromaticAberration = 0.0;
    exactStyle.saturation = 1.0;
    exactStyle.tintOpacity = 0.0;
    exactStyle.adaptiveLuminance = false;
    exactStyle.quality = vkui::VLiquidGlassQuality::High;
    glass->setGlassStyle(exactStyle);
    glass->setBackdrop(view.liquidGlassBackdrop());
    glass->raise();

    view.show();
    QApplication::processEvents();

    QWidget* titleBar = view.titleBar();
    QScrollArea* scrollArea = view.pageScrollArea(0);
    QVERIFY(titleBar != nullptr);
    QVERIFY(scrollArea != nullptr);
    QCOMPARE(titleBar->geometry().top(), 0);
    QCOMPARE(titleBar->height(), GalleryContentView::TitleBarHeight);
    QVERIFY(view.liquidGlassBackdrop() != nullptr);
    QVERIFY(view.liquidGlassBackdrop()->sourceWidget() != nullptr);
    QCOMPARE(view.liquidGlassBackdrop()->sourceWidget()->objectName(),
             QStringLiteral("galleryPages"));
    QVERIFY(!titleBar->autoFillBackground());
    QVERIFY(!titleBar->testAttribute(Qt::WA_StyledBackground));
    QCOMPARE(scrollArea->geometry().top(), 0);
    QVERIFY(titleBarControl->isVisible());
    QCOMPARE(titleBarControl->geometry().center().y(), titleBar->geometry().center().y());

    QCOMPARE(page->mapTo(scrollArea->viewport(), QPoint{}).y(), GalleryContentView::TitleBarHeight);
    QVERIFY(scrollArea->verticalScrollBar()->maximum() > GalleryContentView::TitleBarHeight + 20);

    scrollArea->verticalScrollBar()->setValue(GalleryContentView::TitleBarHeight);
    QApplication::processEvents();
    QCOMPARE(page->mapTo(scrollArea->viewport(), QPoint{}).y(), 0);

    scrollArea->verticalScrollBar()->setValue(GalleryContentView::TitleBarHeight + 20);
    QApplication::processEvents();
    QCOMPARE(page->mapTo(scrollArea->viewport(), QPoint{}).y(), -20);
    QCOMPARE(titleBar->geometry().top(), 0);

    const QImage capturedBackdrop = glass->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QColor capturedColor = capturedBackdrop.pixelColor(capturedBackdrop.rect().center());
    QVERIFY(capturedColor.red() > 190);
    QVERIFY(capturedColor.green() < 100);
    QVERIFY(capturedColor.blue() < 90);

    QImage rendered(view.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    view.render(&rendered);
    QCOMPARE(rendered.pixelColor(10, 100), pageColor);
    QCOMPARE(rendered.pixelColor(10, 10), pageColor);
}

QTEST_MAIN(GalleryContentViewTest)
#include "tst_gallerycontentview.moc"
