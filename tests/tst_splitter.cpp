// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QImage>
#include <QSplitterHandle>
#include <QWidget>
#include <QtTest>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/controls/VSplitter.h>

class SplitterTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void thinHandleKeepsExpandedGrabArea_data();
    void thinHandleKeepsExpandedGrabArea();
    void hoverPaintsAccentSeparator();
};

void SplitterTest::thinHandleKeepsExpandedGrabArea_data() {
    QTest::addColumn<Qt::Orientation>("orientation");
    QTest::newRow("horizontal") << Qt::Horizontal;
    QTest::newRow("vertical") << Qt::Vertical;
}

void SplitterTest::thinHandleKeepsExpandedGrabArea() {
    QFETCH(Qt::Orientation, orientation);

    vkui::VSplitter splitter(orientation);
    splitter.addWidget(new QWidget());
    splitter.addWidget(new QWidget());
    splitter.resize(320, 240);
    splitter.show();
    QApplication::processEvents();

    QSplitterHandle* handle = splitter.handle(1);
    QVERIFY(handle != nullptr);
    QCOMPARE(splitter.handleWidth(), 1);

    const int layoutExtent = orientation == Qt::Horizontal ? splitter.width() : splitter.height();
    QCOMPARE(splitter.sizes().constFirst() + splitter.sizes().constLast() +
                 splitter.handleWidth(),
             layoutExtent);

    const int widgetExtent = orientation == Qt::Horizontal ? handle->width() : handle->height();
    const int visualExtent = orientation == Qt::Horizontal ? handle->contentsRect().width()
                                                            : handle->contentsRect().height();
    QVERIFY(widgetExtent > splitter.handleWidth());
    QCOMPARE(visualExtent, splitter.handleWidth());
    QCOMPARE(handle->cursor().shape(),
             orientation == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
}

void SplitterTest::hoverPaintsAccentSeparator() {
    vkui::VSplitter splitter(Qt::Horizontal);
    splitter.addWidget(new QWidget());
    splitter.addWidget(new QWidget());
    splitter.resize(320, 240);
    splitter.show();
    QApplication::processEvents();

    QSplitterHandle* handle = splitter.handle(1);
    QVERIFY(handle != nullptr);

    QTest::mouseMove(handle, handle->rect().center());
    QApplication::processEvents();

    // The offscreen platform does not composite masked overlap widgets into render().
    // Clearing only the test widget's mask exposes the same paint path for pixel verification.
    handle->clearMask();
    QImage image(handle->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    handle->render(&image);

    const QColor expected = vkui::VkThemeManager::instance()->theme().colors().accent;
    const QRect visualRect = handle->contentsRect();
    const int separatorX = visualRect.center().x();
    QCOMPARE(image.pixelColor(separatorX, visualRect.top()), expected);
    QCOMPARE(image.pixelColor(separatorX, visualRect.center().y()), expected);
    QCOMPARE(image.pixelColor(separatorX, visualRect.bottom()), expected);

    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(handle, &leaveEvent);
    image.fill(Qt::transparent);
    handle->render(&image);
    QVERIFY(image.pixelColor(visualRect.center()) != expected);
}

QTEST_MAIN(SplitterTest)

#include "tst_splitter.moc"
