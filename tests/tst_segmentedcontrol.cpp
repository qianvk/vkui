// SPDX-License-Identifier: MIT

#include <QSignalSpy>
#include <QStyle>
#include <QtTest>
#include <vkui/widgets/controls/VkSegmentedControl.h>

class SegmentedControlTest final : public QObject {
    Q_OBJECT

  private slots:
    void insertionAndRemovalMaintainSelection();
    void contentAndEnabledStateRoundTrip();
    void currentIndexSignalsOnlyOnChange();
    void keyboardNavigationSkipsDisabledSegments();
    void rightToLeftReversesVisualArrowDirection();
    void clearRestoresEmptyInvariant();
};

void SegmentedControlTest::insertionAndRemovalMaintainSelection() {
    vkui::VkSegmentedControl control;
    QCOMPARE(control.currentIndex(), -1);
    QCOMPARE(control.addSegment(QStringLiteral("One")), 0);
    QCOMPARE(control.currentIndex(), 0);
    control.addSegment(QStringLiteral("Three"));
    QCOMPARE(control.insertSegment(1, QIcon{}, QStringLiteral("Two")), 1);
    QCOMPARE(control.count(), 3);
    QCOMPARE(control.segmentText(1), QStringLiteral("Two"));
    control.setCurrentIndex(1);
    control.removeSegment(1);
    QCOMPARE(control.count(), 2);
    QCOMPARE(control.currentIndex(), 1);
    QCOMPARE(control.segmentText(1), QStringLiteral("Three"));
}

void SegmentedControlTest::contentAndEnabledStateRoundTrip() {
    vkui::VkSegmentedControl control;
    control.addSegment(QStringLiteral("One"));
    control.addSegment(QStringLiteral("Two"));
    control.setSegmentText(1, QStringLiteral("Second"));
    QCOMPARE(control.segmentText(1), QStringLiteral("Second"));
    const QIcon icon = QApplication::style()->standardIcon(QStyle::SP_DialogApplyButton);
    control.setSegmentIcon(1, icon);
    QCOMPARE(control.segmentIcon(1).cacheKey(), icon.cacheKey());
    control.setSegmentEnabled(1, false);
    QVERIFY(!control.isSegmentEnabled(1));
    QVERIFY(control.isSegmentEnabled(0));
    control.setSegmentAccessibleName(1, QStringLiteral("Second accessible segment"));
    QCOMPARE(control.segmentAccessibleName(1), QStringLiteral("Second accessible segment"));
    control.setSegmentText(1, QStringLiteral("Renamed"));
    QCOMPARE(control.segmentAccessibleName(1), QStringLiteral("Second accessible segment"));
    control.setSegmentAccessibleName(1, {});
    QCOMPARE(control.segmentAccessibleName(1), QStringLiteral("Renamed"));
    QVERIFY(control.segmentAccessibleName(99).isEmpty());
}

void SegmentedControlTest::currentIndexSignalsOnlyOnChange() {
    vkui::VkSegmentedControl control;
    control.addSegment(QStringLiteral("One"));
    control.addSegment(QStringLiteral("Two"));
    QSignalSpy spy(&control, &vkui::VkSegmentedControl::currentIndexChanged);
    control.setCurrentIndex(1);
    QCOMPARE(spy.count(), 1);
    control.setCurrentIndex(1);
    QCOMPARE(spy.count(), 1);
    control.setCurrentIndex(-1);
    QCOMPARE(spy.count(), 2);
}

void SegmentedControlTest::keyboardNavigationSkipsDisabledSegments() {
    vkui::VkSegmentedControl control;
    control.addSegment(QStringLiteral("One"));
    control.addSegment(QStringLiteral("Two"));
    control.addSegment(QStringLiteral("Three"));
    control.setSegmentEnabled(1, false);
    control.resize(control.sizeHint());
    control.show();
    control.setFocus();
    QTest::keyClick(&control, Qt::Key_Right);
    QCOMPARE(control.currentIndex(), 2);
    QTest::keyClick(&control, Qt::Key_Left);
    QCOMPARE(control.currentIndex(), 0);
}

void SegmentedControlTest::rightToLeftReversesVisualArrowDirection() {
    vkui::VkSegmentedControl control;
    control.setLayoutDirection(Qt::RightToLeft);
    control.addSegment(QStringLiteral("One"));
    control.addSegment(QStringLiteral("Two"));
    control.addSegment(QStringLiteral("Three"));
    control.setCurrentIndex(1);
    control.resize(control.sizeHint());
    control.show();
    control.setFocus();
    QTest::keyClick(&control, Qt::Key_Right);
    QCOMPARE(control.currentIndex(), 0);
    control.setCurrentIndex(1);
    QTest::keyClick(&control, Qt::Key_Left);
    QCOMPARE(control.currentIndex(), 2);
}

void SegmentedControlTest::clearRestoresEmptyInvariant() {
    vkui::VkSegmentedControl control;
    control.addSegment(QStringLiteral("One"));
    control.addSegment(QStringLiteral("Two"));
    QSignalSpy spy(&control, &vkui::VkSegmentedControl::currentIndexChanged);
    control.clear();
    QCOMPARE(control.count(), 0);
    QCOMPARE(control.currentIndex(), -1);
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(SegmentedControlTest)
#include "tst_segmentedcontrol.moc"
