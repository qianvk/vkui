// SPDX-License-Identifier: MIT

#include <QAccessible>
#include <QSignalSpy>
#include <QtTest>
#include <vkui/widgets/controls/VSwitch.h>

class SwitchTest final : public QObject {
    Q_OBJECT

  private slots:
    void startsUncheckedAndCheckable();
    void mouseAndKeyboardToggle();
    void disabledSwitchDoesNotToggle();
    void emitsInheritedToggledSignal();
    void rapidRetargetingKeepsLogicalState();
    void sizeHintsReflectControlSize();
    void exactExtentOverridesAndResetsPreset();
    void rightToLeftRendersAndReportsCheckedState();
};

void SwitchTest::startsUncheckedAndCheckable() {
    vkui::VSwitch control;
    QVERIFY(control.isCheckable());
    QVERIFY(!control.isChecked());
    QVERIFY(control.sizeHint().isValid());
}

void SwitchTest::mouseAndKeyboardToggle() {
    vkui::VSwitch control;
    control.show();
    QTest::qWait(1);
    QTest::mouseClick(&control, Qt::LeftButton, Qt::NoModifier, control.rect().center());
    QVERIFY(control.isChecked());
    control.setFocus();
    QTest::keyClick(&control, Qt::Key_Space);
    QVERIFY(!control.isChecked());
}

void SwitchTest::disabledSwitchDoesNotToggle() {
    vkui::VSwitch control;
    control.setEnabled(false);
    control.show();
    QTest::mouseClick(&control, Qt::LeftButton, Qt::NoModifier, control.rect().center());
    QVERIFY(!control.isChecked());
    QTest::keyClick(&control, Qt::Key_Space);
    QVERIFY(!control.isChecked());
}

void SwitchTest::emitsInheritedToggledSignal() {
    vkui::VSwitch control;
    QSignalSpy spy(&control, &vkui::VSwitch::toggled);
    control.toggle();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toBool(), true);
}

void SwitchTest::rapidRetargetingKeepsLogicalState() {
    vkui::VSwitch control;
    control.show();
    for (int index = 0; index < 17; ++index) {
        control.toggle();
        QTest::qWait(4);
    }
    QVERIFY(control.isChecked());
    QTest::qWait(250);
    QVERIFY(control.isChecked());
}

void SwitchTest::sizeHintsReflectControlSize() {
    vkui::VSwitch control;
    control.setControlSize(vkui::VControlSize::Small);
    const QSize small = control.sizeHint();
    control.setControlSize(vkui::VControlSize::Regular);
    const QSize regular = control.sizeHint();
    control.setControlSize(vkui::VControlSize::Large);
    const QSize large = control.sizeHint();
    QVERIFY(regular.width() > small.width());
    QVERIFY(regular.height() > small.height());
    QVERIFY(large.width() > regular.width());
    QVERIFY(large.height() > regular.height());
    QVERIFY(control.minimumSizeHint().width() > 0);
}

void SwitchTest::exactExtentOverridesAndResetsPreset() {
    vkui::VSwitch control;
    QCOMPARE(control.controlExtent(), 18);
    QSignalSpy spy(&control, &vkui::VSwitch::controlExtentChanged);

    control.setControlExtent(31);
    QCOMPARE(control.controlExtent(), 31);
    QCOMPARE(vkui::customControlExtent(control), std::optional<int>(31));
    QCOMPARE(spy.count(), 1);

    control.resetControlExtent();
    QCOMPARE(control.controlExtent(), vkui::controlExtent(vkui::VControlSize::Regular));
    QVERIFY(!vkui::customControlExtent(control));

    control.setControlExtent(500);
    QCOMPARE(control.controlExtent(), vkui::VMaximumControlExtent);
    control.setControlSize(vkui::VControlSize::Small);
    QCOMPARE(control.controlExtent(), vkui::controlExtent(vkui::VControlSize::Small));
    QVERIFY(!vkui::customControlExtent(control));
}

void SwitchTest::rightToLeftRendersAndReportsCheckedState() {
    vkui::VSwitch control;
    control.setLayoutDirection(Qt::RightToLeft);
    control.setAccessibleName(QStringLiteral("Wi-Fi"));
    control.setChecked(true);
    control.resize(control.sizeHint());
    QPixmap rendering(control.size());
    rendering.fill(Qt::transparent);
    control.render(&rendering);
    QVERIFY(!rendering.isNull());

    QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(&control);
    QVERIFY(interface != nullptr);
    QVERIFY(interface->state().checkable);
    QVERIFY(interface->state().checked);
}

QTEST_MAIN(SwitchTest)
#include "tst_switch.moc"
