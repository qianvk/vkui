// SPDX-License-Identifier: MIT

#include <QtTest>
#include <vkui/core/VkMotion.h>
#include <vkui/core/VkThemeManager.h>

class MotionTest final : public QObject {
    Q_OBJECT

  private slots:
    void rolesHaveRestrainedSpecifications();
    void disabledAnimationsCompleteImmediately();
};

void MotionTest::rolesHaveRestrainedSpecifications() {
    auto* manager = vkui::VkThemeManager::instance();
    const bool original = manager->animationsEnabled();
    manager->setAnimationsEnabled(true);

    QCOMPARE(vkui::motionSpec(vkui::VkMotionRole::Immediate).durationMs, 0);
    for (const vkui::VkMotionRole role : {
             vkui::VkMotionRole::StateTransition,
             vkui::VkMotionRole::Enter,
             vkui::VkMotionRole::Exit,
             vkui::VkMotionRole::EmphasizedEnter,
             vkui::VkMotionRole::EmphasizedExit,
         }) {
        const vkui::VkMotionSpec spec = vkui::motionSpec(role);
        QVERIFY(spec.durationMs > 0);
        QVERIFY(spec.durationMs <= 400);
        QVERIFY(spec.easing.type() != QEasingCurve::OutBounce);
        QVERIFY(spec.easing.type() != QEasingCurve::OutElastic);
    }
    manager->setAnimationsEnabled(original);
}

void MotionTest::disabledAnimationsCompleteImmediately() {
    auto* manager = vkui::VkThemeManager::instance();
    const bool original = manager->animationsEnabled();
    QSignalSpy spy(manager, &vkui::VkThemeManager::animationsEnabledChanged);
    manager->setAnimationsEnabled(false);
    QCOMPARE(vkui::motionSpec(vkui::VkMotionRole::EmphasizedEnter).durationMs, 0);
    if (original) {
        QCOMPARE(spy.count(), 1);
    }
    manager->setAnimationsEnabled(original);
}

QTEST_MAIN(MotionTest)
#include "tst_motion.moc"
