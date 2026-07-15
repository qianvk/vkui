// SPDX-License-Identifier: MIT

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>

#include <vkui/widgets/overlays/VkPopover.h>

class PopoverInteractionTest : public QObject {
    Q_OBJECT

private slots:
    void currentAnchorClickTogglesClosed();
    void anotherAnchorClickSurvivesOldPopoverClose();
};

namespace {

void clickThroughPopover(vkui::VkPopover& popover, const QWidget& target) {
    const QPoint globalPoint = target.mapToGlobal(target.rect().center());
    QTest::mouseClick(&popover, Qt::LeftButton, Qt::NoModifier,
                      popover.mapFromGlobal(globalPoint));
}

} // namespace

void PopoverInteractionTest::currentAnchorClickTogglesClosed() {
    QWidget window;
    window.resize(480, 320);
    QPushButton anchor(QStringLiteral("Anchor"), &window);
    anchor.setGeometry(340, 12, 96, 32);
    window.show();

    vkui::VkPopover popover(&window);
    auto* content = new QWidget;
    content->setFixedSize(240, 160);
    popover.setContentWidget(content);
    connect(&anchor, &QPushButton::clicked, &popover, [&popover] {
        if (popover.isOpen()) {
            popover.closeImmediately();
        }
    });

    QSignalSpy clickSpy(&anchor, &QPushButton::clicked);
    popover.openFor(&anchor);
    QVERIFY(popover.isOpen());

    clickThroughPopover(popover, anchor);
    QTRY_COMPARE(clickSpy.count(), 1);
    QTRY_VERIFY(!popover.isOpen());
    QTRY_VERIFY(anchor.underMouse());
}

void PopoverInteractionTest::anotherAnchorClickSurvivesOldPopoverClose() {
    QWidget window;
    window.resize(560, 360);
    QPushButton first(QStringLiteral("First"), &window);
    QPushButton second(QStringLiteral("Second"), &window);
    first.setGeometry(320, 12, 96, 32);
    second.setGeometry(424, 12, 96, 32);
    window.show();

    vkui::VkPopover popover(&window);
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

QTEST_MAIN(PopoverInteractionTest)

#include "tst_popoverinteraction.moc"
