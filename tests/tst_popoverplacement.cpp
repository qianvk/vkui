// SPDX-License-Identifier: MIT

#include "widgets/overlays/private/VkPopoverPlacementEngine_p.h"

#include <QApplication>
#include <QLabel>
#include <QPointer>
#include <QtTest>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/overlays/VkPopover.h>

namespace {

vkui::VkPopoverPlacementInput baseInput() {
    vkui::VkPopoverPlacementInput input;
    input.anchorRect = QRectF(480.0, 320.0, 40.0, 28.0);
    input.contentSize = QSizeF(180.0, 96.0);
    input.availableGeometry = QRectF(0.0, 0.0, 1000.0, 700.0);
    input.screenMargin = 12.0;
    input.anchorGap = 6.0;
    input.bodyCornerRadius = 12.0;
    input.arrowWidth = 18.0;
    input.arrowDepth = 10.0;
    input.contentMargins = QMarginsF(12.0, 12.0, 12.0, 12.0);
    input.outerMargin = 24.0;
    return input;
}

void verifyInsideAvailableGeometry(const vkui::VkPopoverPlacementInput& input,
                                   const vkui::VkPopoverPlacementResult& result) {
    const QRectF usable = input.availableGeometry.adjusted(
        input.screenMargin, input.screenMargin, -input.screenMargin, -input.screenMargin);
    QVERIFY(usable.adjusted(-1.0, -1.0, 1.0, 1.0).contains(QRectF(result.popupRect)));
    const QRectF localPopup(QPointF(0.0, 0.0), QSizeF(result.popupRect.size()));
    QVERIFY(localPopup.adjusted(-0.1, -0.1, 0.1, 0.1).contains(result.bodyRect));
    QVERIFY(result.bodyRect.contains(result.contentRect));
}

} // namespace

class PopoverPlacementTest final : public QObject {
    Q_OBJECT

  private slots:
    void forcedPlacements_data();
    void forcedPlacements();
    void automaticPlacementAtEveryEdge_data();
    void automaticPlacementAtEveryEdge();
    void cornersRemainValid_data();
    void cornersRemainValid();
    void supportsNegativeGlobalCoordinates();
    void oversizedContentIsClamped();
    void arrowIsRecalculatedAfterBodyClamping();
    void suppliedAnchorSubRectangleChangesAim();
    void rightToLeftChangesHorizontalFallbackPriority();
    void rejectsGeometryWithoutRoomForAnArrow();
    void anchorDestructionClosesAnOpenPopover();
    void anchorMovementRepositionsWithoutRecreation();
    void interruptedAnimationRetargetsCleanly();
};

void PopoverPlacementTest::forcedPlacements_data() {
    QTest::addColumn<int>("placement");
    for (const auto placement : {vkui::VkPopoverPlacement::Below, vkui::VkPopoverPlacement::Above,
                                 vkui::VkPopoverPlacement::Right, vkui::VkPopoverPlacement::Left}) {
        QTest::newRow(qPrintable(QString::number(static_cast<int>(placement))))
            << static_cast<int>(placement);
    }
}

void PopoverPlacementTest::forcedPlacements() {
    QFETCH(int, placement);
    auto input = baseInput();
    input.preferredPlacement = static_cast<vkui::VkPopoverPlacement>(placement);
    const auto result = vkui::VkPopoverPlacementEngine::calculate(input);
    QVERIFY(result.isValid());
    QCOMPARE(result.resolvedPlacement, input.preferredPlacement);
    verifyInsideAvailableGeometry(input, result);
}

void PopoverPlacementTest::automaticPlacementAtEveryEdge_data() {
    QTest::addColumn<QRectF>("anchor");
    QTest::addColumn<int>("expected");
    QTest::newRow("top") << QRectF(480.0, 12.0, 40.0, 20.0)
                         << static_cast<int>(vkui::VkPopoverPlacement::Below);
    QTest::newRow("bottom") << QRectF(480.0, 668.0, 40.0, 20.0)
                            << static_cast<int>(vkui::VkPopoverPlacement::Above);
    QTest::newRow("left") << QRectF(12.0, 330.0, 20.0, 28.0)
                          << static_cast<int>(vkui::VkPopoverPlacement::Right);
    QTest::newRow("right") << QRectF(968.0, 330.0, 20.0, 28.0)
                           << static_cast<int>(vkui::VkPopoverPlacement::Left);
}

void PopoverPlacementTest::automaticPlacementAtEveryEdge() {
    QFETCH(QRectF, anchor);
    QFETCH(int, expected);
    auto input = baseInput();
    input.anchorRect = anchor;
    const auto result = vkui::VkPopoverPlacementEngine::calculate(input);
    QVERIFY(result.isValid());
    QCOMPARE(result.resolvedPlacement, static_cast<vkui::VkPopoverPlacement>(expected));
    verifyInsideAvailableGeometry(input, result);
}

void PopoverPlacementTest::cornersRemainValid_data() {
    QTest::addColumn<QRectF>("anchor");
    QTest::newRow("top-left") << QRectF(8.0, 8.0, 18.0, 18.0);
    QTest::newRow("top-right") << QRectF(974.0, 8.0, 18.0, 18.0);
    QTest::newRow("bottom-left") << QRectF(8.0, 674.0, 18.0, 18.0);
    QTest::newRow("bottom-right") << QRectF(974.0, 674.0, 18.0, 18.0);
}

void PopoverPlacementTest::cornersRemainValid() {
    QFETCH(QRectF, anchor);
    auto input = baseInput();
    input.anchorRect = anchor;
    const auto result = vkui::VkPopoverPlacementEngine::calculate(input);
    QVERIFY(result.isValid());
    verifyInsideAvailableGeometry(input, result);
}

void PopoverPlacementTest::supportsNegativeGlobalCoordinates() {
    auto input = baseInput();
    input.availableGeometry = QRectF(-1920.0, -300.0, 1920.0, 1080.0);
    input.anchorRect = QRectF(-1850.0, 120.0, 30.0, 28.0);
    const auto result = vkui::VkPopoverPlacementEngine::calculate(input);
    QVERIFY(result.isValid());
    QVERIFY(result.popupRect.left() < 0);
    verifyInsideAvailableGeometry(input, result);
}

void PopoverPlacementTest::oversizedContentIsClamped() {
    auto input = baseInput();
    input.anchorRect = QRectF(390.0, 270.0, 20.0, 20.0);
    input.availableGeometry = QRectF(100.0, 100.0, 600.0, 400.0);
    input.contentSize = QSizeF(2400.0, 1800.0);
    const auto result = vkui::VkPopoverPlacementEngine::calculate(input);
    QVERIFY(result.isValid());
    verifyInsideAvailableGeometry(input, result);
    QVERIFY(result.contentRect.width() < input.contentSize.width());
    QVERIFY(result.contentRect.height() < input.contentSize.height());
}

void PopoverPlacementTest::arrowIsRecalculatedAfterBodyClamping() {
    auto input = baseInput();
    input.preferredPlacement = vkui::VkPopoverPlacement::Below;
    input.anchorRect = QRectF(3.0, 180.0, 10.0, 20.0);
    const auto result = vkui::VkPopoverPlacementEngine::calculate(input);
    QVERIFY(result.isValid());
    const qreal globalTipX = result.popupRect.left() + result.arrowTip.x();
    QVERIFY(globalTipX < input.anchorRect.right() + input.arrowWidth + 1.0);
    QVERIFY(result.arrowBaseCenter.x() >=
            result.bodyRect.left() + input.bodyCornerRadius + input.arrowWidth / 2.0 - 0.1);
}

void PopoverPlacementTest::suppliedAnchorSubRectangleChangesAim() {
    auto leftInput = baseInput();
    leftInput.preferredPlacement = vkui::VkPopoverPlacement::Below;
    leftInput.anchorRect = QRectF(430.0, 300.0, 16.0, 16.0);
    auto rightInput = leftInput;
    rightInput.anchorRect.moveLeft(550.0);
    const auto left = vkui::VkPopoverPlacementEngine::calculate(leftInput);
    const auto right = vkui::VkPopoverPlacementEngine::calculate(rightInput);
    QVERIFY(left.isValid());
    QVERIFY(right.isValid());
    const qreal leftTip = left.popupRect.left() + left.arrowTip.x();
    const qreal rightTip = right.popupRect.left() + right.arrowTip.x();
    QVERIFY(rightTip > leftTip + 80.0);
}

void PopoverPlacementTest::rightToLeftChangesHorizontalFallbackPriority() {
    auto leftToRight = baseInput();
    leftToRight.availableGeometry = QRectF(0.0, 0.0, 720.0, 190.0);
    leftToRight.anchorRect = QRectF(350.0, 82.0, 20.0, 20.0);
    leftToRight.contentSize = QSizeF(120.0, 500.0);
    leftToRight.layoutDirection = Qt::LeftToRight;
    auto rightToLeft = leftToRight;
    rightToLeft.layoutDirection = Qt::RightToLeft;
    const auto ltr = vkui::VkPopoverPlacementEngine::calculate(leftToRight);
    const auto rtl = vkui::VkPopoverPlacementEngine::calculate(rightToLeft);
    QVERIFY(ltr.isValid());
    QVERIFY(rtl.isValid());
    QCOMPARE(ltr.resolvedPlacement, vkui::VkPopoverPlacement::Right);
    QCOMPARE(rtl.resolvedPlacement, vkui::VkPopoverPlacement::Left);
}

void PopoverPlacementTest::rejectsGeometryWithoutRoomForAnArrow() {
    auto input = baseInput();
    input.availableGeometry = QRectF(0.0, 0.0, 30.0, 30.0);
    input.screenMargin = 12.0;
    input.anchorRect = QRectF(12.0, 12.0, 4.0, 4.0);
    QVERIFY(!vkui::VkPopoverPlacementEngine::calculate(input).isValid());
}

void PopoverPlacementTest::anchorDestructionClosesAnOpenPopover() {
    auto* manager = vkui::VkThemeManager::instance();
    const bool animations = manager->animationsEnabled();
    manager->setAnimationsEnabled(false);
    auto* anchor = new QWidget;
    anchor->resize(80, 30);
    anchor->show();
    vkui::VkPopover popover;
    popover.setContentWidget(new QLabel(QStringLiteral("Content")));
    popover.openFor(anchor);
    QTRY_VERIFY(popover.isOpen());
    delete anchor;
    QTRY_VERIFY(!popover.isOpen());
    manager->setAnimationsEnabled(animations);
}

void PopoverPlacementTest::anchorMovementRepositionsWithoutRecreation() {
    auto* manager = vkui::VkThemeManager::instance();
    const bool animations = manager->animationsEnabled();
    manager->setAnimationsEnabled(false);
    QWidget anchor;
    anchor.setGeometry(100, 100, 80, 30);
    anchor.show();
    vkui::VkPopover popover;
    popover.setContentWidget(new QLabel(QStringLiteral("Content")));
    popover.openFor(&anchor);
    QTRY_VERIFY(popover.isOpen());
    const QPoint original = popover.pos();
    anchor.move(260, 180);
    QTRY_VERIFY(popover.pos() != original);
    QVERIFY(popover.isOpen());
    popover.closeImmediately();
    manager->setAnimationsEnabled(animations);
}

void PopoverPlacementTest::interruptedAnimationRetargetsCleanly() {
    auto* manager = vkui::VkThemeManager::instance();
    const bool animations = manager->animationsEnabled();
    manager->setAnimationsEnabled(true);
    QWidget anchor;
    anchor.setGeometry(180, 140, 80, 30);
    anchor.show();
    vkui::VkPopover popover;
    popover.setContentWidget(new QLabel(QStringLiteral("Content")));
    QSignalSpy openedSpy(&popover, &vkui::VkPopover::opened);
    QSignalSpy closedSpy(&popover, &vkui::VkPopover::closed);

    popover.openFor(&anchor);
    QTest::qWait(20);
    popover.closeAnimated();
    QTest::qWait(20);
    popover.openFor(&anchor);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 1000);
    QCOMPARE(closedSpy.count(), 0);
    QVERIFY(popover.isOpen());

    popover.closeAnimated();
    QTRY_COMPARE_WITH_TIMEOUT(closedSpy.count(), 1, 1000);
    QVERIFY(!popover.isOpen());
    manager->setAnimationsEnabled(animations);
}

QTEST_MAIN(PopoverPlacementTest)
#include "tst_popoverplacement.moc"
