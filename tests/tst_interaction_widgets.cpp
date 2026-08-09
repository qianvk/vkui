// SPDX-License-Identifier: MIT

#include <vkui/InteractionWidgets.h>

#include <QPushButton>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>

using namespace vkui::vk::widgets;

class InteractionWidgetsTest final : public QObject
{
    Q_OBJECT

private slots:
    void blockLeaseAndSemanticControlNavigation();
};

void InteractionWidgetsTest::blockLeaseAndSemanticControlNavigation()
{
    QWidget root;
    auto *layout = new QVBoxLayout(&root);
    auto *first = new QPushButton(QStringLiteral("First"), &root);
    auto *second = new QPushButton(QStringLiteral("Second"), &root);
    layout->addWidget(first);
    layout->addWidget(second);
    root.show();
    QCoreApplication::processEvents();

    VkBlockRegistry registry;
    VkBlockDescriptor descriptor;
    descriptor.id = QStringLiteral("block.controls");
    descriptor.panelId = QStringLiteral("panel.main");
    descriptor.widget = &root;
    descriptor.focus = [&root] {
        return VkWidgetBlockNavigator::focus(&root);
    };
    descriptor.navigate = [&root](const int h, const int v) {
        return VkWidgetBlockNavigator::navigate(&root, h, v);
    };
    descriptor.activate = [&root] {
        return VkWidgetBlockNavigator::activate(&root);
    };

    VkBlockLease lease = registry.registerBlock(std::move(descriptor));
    QVERIFY(lease);
    QVERIFY(registry.focus(QStringLiteral("block.controls")));
    QVERIFY(first->property("vkKeyboardCurrent").toBool());
    QVERIFY(registry.navigate(QStringLiteral("block.controls"), 0, 1));
    QVERIFY(second->property("vkKeyboardCurrent").toBool());
    lease.reset();
    QVERIFY(!registry.block(QStringLiteral("block.controls")));
}

QTEST_MAIN(InteractionWidgetsTest)
#include "tst_interaction_widgets.moc"
