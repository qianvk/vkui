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

    view.show();
    QApplication::processEvents();

    QWidget* titleBar = view.titleBar();
    QScrollArea* scrollArea = view.pageScrollArea(0);
    QVERIFY(titleBar != nullptr);
    QVERIFY(scrollArea != nullptr);
    QCOMPARE(titleBar->geometry().top(), 0);
    QCOMPARE(titleBar->height(), GalleryContentView::TitleBarHeight);
    QVERIFY(!titleBar->autoFillBackground());
    QVERIFY(!titleBar->testAttribute(Qt::WA_StyledBackground));
    QCOMPARE(scrollArea->geometry().top(), 0);
    QVERIFY(titleBarControl->isVisible());
    QCOMPARE(titleBarControl->geometry().center().y(), titleBar->geometry().center().y());

    QCOMPARE(page->mapTo(scrollArea->viewport(), QPoint{}).y(),
             GalleryContentView::TitleBarHeight);
    QVERIFY(scrollArea->verticalScrollBar()->maximum() >
            GalleryContentView::TitleBarHeight + 20);

    scrollArea->verticalScrollBar()->setValue(GalleryContentView::TitleBarHeight);
    QApplication::processEvents();
    QCOMPARE(page->mapTo(scrollArea->viewport(), QPoint{}).y(), 0);

    scrollArea->verticalScrollBar()->setValue(GalleryContentView::TitleBarHeight + 20);
    QApplication::processEvents();
    QCOMPARE(page->mapTo(scrollArea->viewport(), QPoint{}).y(), -20);
    QCOMPARE(titleBar->geometry().top(), 0);

    QImage rendered(view.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    view.render(&rendered);
    QCOMPARE(rendered.pixelColor(10, 100), pageColor);
    QCOMPARE(rendered.pixelColor(10, 10), pageColor);
}

QTEST_MAIN(GalleryContentViewTest)
#include "tst_gallerycontentview.moc"
