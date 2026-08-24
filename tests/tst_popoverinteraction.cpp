// SPDX-License-Identifier: MIT

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/overlays/VPopover.h>
#include <vkui/widgets/style/VStyle.h>

class PopoverInteractionTest : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void currentAnchorClickTogglesClosed();
    void anotherAnchorClickSurvivesOldPopoverClose();
    void popupContentInheritsResponsiveFont();
};

namespace {

void clickThroughPopover(vkui::VPopover& popover, const QWidget& target) {
    const QPoint globalPoint = target.mapToGlobal(target.rect().center());
    QTest::mouseClick(&popover, Qt::LeftButton, Qt::NoModifier, popover.mapFromGlobal(globalPoint));
}

} // namespace

void PopoverInteractionTest::initTestCase() {
    vkui::installVkUi(*qApp);
}

void PopoverInteractionTest::currentAnchorClickTogglesClosed() {
    QWidget window;
    window.resize(480, 320);
    QPushButton anchor(QStringLiteral("Anchor"), &window);
    anchor.setGeometry(340, 12, 96, 32);
    window.show();

    vkui::VPopover popover(&window);
    auto* content = new QWidget;
    content->setFixedSize(240, 160);
    popover.setContentWidget(content);
    connect(&anchor, &QPushButton::clicked, &popover,
            [&popover, &anchor] { popover.toggleFor(&anchor); });

    QSignalSpy clickSpy(&anchor, &QPushButton::clicked);
    QTest::mouseClick(&anchor, Qt::LeftButton);
    QTRY_VERIFY(popover.isOpen());

    clickThroughPopover(popover, anchor);
    QTRY_COMPARE(clickSpy.count(), 2);
    QTRY_VERIFY(!popover.isOpen());

    QTest::mouseClick(&anchor, Qt::LeftButton);
    QTRY_COMPARE(clickSpy.count(), 3);
    QTRY_VERIFY(popover.isOpen());

    // A second logical click during the exit animation reverses the same
    // state machine instead of waiting for a stale close to complete.
    anchor.click();
    QVERIFY(!popover.isOpen());
    anchor.click();
    QVERIFY(popover.isOpen());
}

void PopoverInteractionTest::anotherAnchorClickSurvivesOldPopoverClose() {
    QWidget window;
    window.resize(560, 360);
    QPushButton first(QStringLiteral("First"), &window);
    QPushButton second(QStringLiteral("Second"), &window);
    first.setGeometry(320, 12, 96, 32);
    second.setGeometry(424, 12, 96, 32);
    window.show();

    vkui::VPopover popover(&window);
    auto* content = new QWidget;
    content->setFixedSize(260, 180);
    popover.setContentWidget(content);
    connect(&second, &QPushButton::clicked, &popover,
            [&popover, &second] { popover.openFor(&second); });

    QSignalSpy secondClickSpy(&second, &QPushButton::clicked);
    popover.openFor(&first);
    QVERIFY(popover.isOpen());

    clickThroughPopover(popover, second);
    QTRY_COMPARE(secondClickSpy.count(), 1);
    QTRY_VERIFY(popover.isOpen());
    QTRY_VERIFY(second.underMouse());
    QVERIFY(!first.underMouse());
}

void PopoverInteractionTest::popupContentInheritsResponsiveFont() {
    auto* manager = vkui::VkThemeManager::instance();
    const int originalLevel = manager->textSizeLevel();
    manager->setTextSizeLevel(vkui::VkMinimumTextSizeLevel);

    QWidget window;
    window.show();
    vkui::VPopover popover(&window);
    auto* content = new QWidget;
    auto* label = new QLabel(QStringLiteral("Popover typography"), content);
    popover.setContentWidget(content);
    QCoreApplication::processEvents();
    const int smallHeight = label->fontMetrics().height();
    QCOMPARE(popover.font(), window.font());
    QCOMPARE(content->font(), window.font());
    QCOMPARE(label->font(), window.font());

    manager->setTextSizeLevel(vkui::VkMaximumTextSizeLevel);
    QCoreApplication::processEvents();
    QCOMPARE(popover.font(), window.font());
    QCOMPARE(content->font(), window.font());
    QCOMPARE(label->font(), window.font());
    QVERIFY(label->fontMetrics().height() > smallHeight);

    manager->setTextSizeLevel(originalLevel);
    QCoreApplication::processEvents();
}

QTEST_MAIN(PopoverInteractionTest)

#include "tst_popoverinteraction.moc"
