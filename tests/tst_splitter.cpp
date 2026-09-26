// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QImage>
#include <QSplitterHandle>
#include <QWidget>
#include <QtTest>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/controls/VSplitter.h>

class SplitterTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void stableHandleSeparatesInputFromPainting_data();
    void stableHandleSeparatesInputFromPainting();
    void hoverPaintsAccentSeparator();
    void collapsedBoundaryUsesExpandedPanelBackground();
    void movingHandleReconcilesStationaryPointer();
    void clickEmitsHandleIndex();
    void dragDoesNotEmitClick();
};

void SplitterTest::stableHandleSeparatesInputFromPainting_data() {
    QTest::addColumn<Qt::Orientation>("orientation");
    QTest::newRow("horizontal") << Qt::Horizontal;
    QTest::newRow("vertical") << Qt::Vertical;
}

void SplitterTest::stableHandleSeparatesInputFromPainting() {
    QFETCH(Qt::Orientation, orientation);

    vkui::VSplitter splitter(orientation);
    splitter.addWidget(new QWidget());
    splitter.addWidget(new QWidget());
    splitter.resize(320, 240);
    splitter.show();
    QApplication::processEvents();

    QSplitterHandle* handle = splitter.handle(1);
    QVERIFY(handle != nullptr);
    QCOMPARE(splitter.handleWidth(), 5);

    const int layoutExtent = orientation == Qt::Horizontal ? splitter.width() : splitter.height();
    QCOMPARE(splitter.sizes().constFirst() + splitter.sizes().constLast() + splitter.handleWidth(),
             layoutExtent);

    const int widgetExtent = orientation == Qt::Horizontal ? handle->width() : handle->height();
    const int visualExtent = orientation == Qt::Horizontal ? handle->contentsRect().width()
                                                           : handle->contentsRect().height();
    QCOMPARE(widgetExtent, splitter.handleWidth());
    QCOMPARE(visualExtent, splitter.handleWidth());
    QCOMPARE(handle->cursor().shape(),
             orientation == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
}

void SplitterTest::hoverPaintsAccentSeparator() {
    vkui::VSplitter splitter(Qt::Horizontal);
    auto* first = new QWidget();
    auto* second = new QWidget();
    QPalette firstPalette = first->palette();
    QPalette secondPalette = second->palette();
    const QColor firstColor(37, 67, 97);
    const QColor secondColor(107, 77, 47);
    firstPalette.setColor(first->backgroundRole(), firstColor);
    secondPalette.setColor(second->backgroundRole(), secondColor);
    first->setPalette(firstPalette);
    second->setPalette(secondPalette);
    splitter.addWidget(first);
    splitter.addWidget(second);
    splitter.resize(320, 240);
    splitter.show();
    QApplication::processEvents();

    QSplitterHandle* handle = splitter.handle(1);
    QVERIFY(handle != nullptr);

    const QPoint center = handle->rect().center();
    const QPoint centerGlobal = handle->mapToGlobal(center);
    QCursor::setPos(centerGlobal);
    QTRY_COMPARE(QCursor::pos(), centerGlobal);
    QEnterEvent enterEvent{QPointF(center), QPointF(center), QPointF(centerGlobal)};
    QApplication::sendEvent(handle, &enterEvent);

    QImage image(handle->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    handle->render(&image);

    const QColor expected = vkui::VkThemeManager::instance()->theme().colors().accent;
    const QRect visualRect = handle->contentsRect();
    const int separatorX = visualRect.center().x();
    QCOMPARE(image.pixelColor(visualRect.left(), visualRect.center().y()), firstColor);
    QCOMPARE(image.pixelColor(visualRect.right(), visualRect.center().y()), secondColor);
    QCOMPARE(image.pixelColor(separatorX, visualRect.top()), expected);
    QCOMPARE(image.pixelColor(separatorX, visualRect.center().y()), expected);
    QCOMPARE(image.pixelColor(separatorX, visualRect.bottom()), expected);

    const QPoint outsideGlobal = splitter.mapToGlobal(QPoint(0, 0));
    QCursor::setPos(outsideGlobal);
    QTRY_COMPARE(QCursor::pos(), outsideGlobal);
    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(handle, &leaveEvent);
    image.fill(Qt::transparent);
    handle->render(&image);
    QCOMPARE(image.pixelColor(separatorX, visualRect.center().y()), firstColor);
}

void SplitterTest::collapsedBoundaryUsesExpandedPanelBackground() {
    vkui::VSplitter splitter(Qt::Horizontal);
    auto* first = new QWidget();
    auto* second = new QWidget();
    QPalette firstPalette = first->palette();
    QPalette secondPalette = second->palette();
    const QColor collapsedColor(37, 67, 97);
    const QColor expandedColor(107, 77, 47);
    firstPalette.setColor(first->backgroundRole(), collapsedColor);
    secondPalette.setColor(second->backgroundRole(), expandedColor);
    first->setPalette(firstPalette);
    second->setPalette(secondPalette);
    splitter.addWidget(first);
    splitter.addWidget(second);
    splitter.resize(320, 240);
    splitter.show();
    QApplication::processEvents();
    splitter.setSizes({0, splitter.width() - splitter.handleWidth()});
    QApplication::processEvents();

    QCOMPARE(splitter.sizes().constFirst(), 0);
    QSplitterHandle* handle = splitter.handle(1);
    QVERIFY(handle != nullptr);
    QImage image(handle->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    handle->render(&image);

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QCOMPARE(image.pixelColor(x, y).rgb(), expandedColor.rgb());
        }
    }
}

void SplitterTest::movingHandleReconcilesStationaryPointer() {
    vkui::VSplitter splitter(Qt::Horizontal);
    splitter.addWidget(new QWidget());
    splitter.addWidget(new QWidget());
    splitter.resize(320, 240);
    splitter.show();
    QApplication::processEvents();

    QSplitterHandle* handle = splitter.handle(1);
    QVERIFY(handle != nullptr);
    const QPoint center = handle->rect().center();
    const QPoint stationaryGlobalPosition = handle->mapToGlobal(center);
    QCursor::setPos(stationaryGlobalPosition);
    QTRY_COMPARE(QCursor::pos(), stationaryGlobalPosition);
    QEnterEvent enterEvent{QPointF(center), QPointF(center), QPointF(stationaryGlobalPosition)};
    QApplication::sendEvent(handle, &enterEvent);
    QTRY_COMPARE(splitter.windowHandle()->cursor().shape(), Qt::SplitHCursor);

    splitter.setSizes({0, splitter.width() - splitter.handleWidth()});

    QCOMPARE(QCursor::pos(), stationaryGlobalPosition);
    QTRY_COMPARE(splitter.sizes().constFirst(), 0);
    QTRY_COMPARE(splitter.windowHandle()->cursor().shape(), Qt::ArrowCursor);

    QImage image(handle->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    handle->render(&image);
    const QRgb accent = vkui::VkThemeManager::instance()->theme().colors().accent.rgb();
    for (int y = 0; y < image.height(); ++y) {
        QVERIFY(image.pixelColor(handle->rect().center().x(), y).rgb() != accent);
    }
}

void SplitterTest::clickEmitsHandleIndex() {
    vkui::VSplitter splitter(Qt::Horizontal);
    splitter.addWidget(new QWidget());
    splitter.addWidget(new QWidget());
    splitter.resize(320, 240);
    splitter.show();
    QApplication::processEvents();

    QSignalSpy clicked(&splitter, &vkui::VSplitter::handleClicked);
    QSplitterHandle* handle = splitter.handle(1);
    QVERIFY(handle != nullptr);
    QTest::mouseClick(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());

    QCOMPARE(clicked.count(), 0);
    QTRY_COMPARE(clicked.count(), 1);
    QCOMPARE(clicked.constFirst().constFirst().toInt(), 1);
    QCOMPARE(clicked.constFirst().at(1).value<Qt::MouseButton>(), Qt::LeftButton);

    QTest::mouseClick(handle, Qt::RightButton, Qt::NoModifier, handle->rect().center());
    QTRY_COMPARE(clicked.count(), 2);
    QCOMPARE(clicked.constLast().constFirst().toInt(), 1);
    QCOMPARE(clicked.constLast().at(1).value<Qt::MouseButton>(), Qt::RightButton);
}

void SplitterTest::dragDoesNotEmitClick() {
    vkui::VSplitter splitter(Qt::Horizontal);
    splitter.addWidget(new QWidget());
    splitter.addWidget(new QWidget());
    splitter.resize(320, 240);
    splitter.show();
    QApplication::processEvents();

    QSignalSpy clicked(&splitter, &vkui::VSplitter::handleClicked);
    QSplitterHandle* handle = splitter.handle(1);
    QVERIFY(handle != nullptr);
    const QPoint origin = handle->rect().center();
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, origin);
    QTest::mouseMove(handle, origin + QPoint(QApplication::startDragDistance() + 8, 0), 20);
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier,
                        origin + QPoint(QApplication::startDragDistance() + 8, 0));

    QCOMPARE(clicked.count(), 0);

    QTest::mousePress(handle, Qt::RightButton, Qt::NoModifier, origin);
    QTest::mouseMove(handle, origin + QPoint(0, QApplication::startDragDistance() + 8), 20);
    QTest::mouseRelease(handle, Qt::RightButton, Qt::NoModifier,
                        origin + QPoint(0, QApplication::startDragDistance() + 8));
    QCOMPARE(clicked.count(), 0);
}

QTEST_MAIN(SplitterTest)

#include "tst_splitter.moc"
