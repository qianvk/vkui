// SPDX-License-Identifier: MIT

#include <vkui/Panel.h>

#include <QTest>

using namespace vkui::panel;

class PanelTest final : public QObject
{
    Q_OBJECT

private slots:
    void splitToggleResizeAndNavigate();
};

void PanelTest::splitToggleResizeAndNavigate()
{
    VkPanelLayoutModel model;
    QVERIFY(model.setRoot({QStringLiteral("a"), {}, QLatin1Char('a')}));
    QVERIFY(model.split(
        QStringLiteral("a"),
        {QStringLiteral("b"), {}, QLatin1Char('b')},
        VkPanelSplitAxis::Horizontal));
    QVERIFY(model.split(
        QStringLiteral("b"),
        {QStringLiteral("c"), {}, QLatin1Char('c')},
        VkPanelSplitAxis::Vertical));

    const VkPanelLayoutSnapshot expanded =
        model.snapshot(VkPanelSnapshotMode::Expanded);
    // The synthetic root is intentionally excluded from chooser snapshots.
    QCOMPARE(expanded.regions().size(), 4);
    QVERIFY(model.toggle(QStringLiteral("b")));
    QVERIFY(!model.isVisible(QStringLiteral("b")));
    QVERIFY(model.toggle(QStringLiteral("b")));
    QVERIFY(model.resize(
        QStringLiteral("a"), VkSpatialDirection::Right, 0.02));

    const QVector<VkSpatialWindow> candidates{
        {1, QRectF(0, 0, 100, 100)},
        {2, QRectF(100, 0, 100, 100)},
        {3, QRectF(200, 0, 100, 100)},
    };
    QCOMPARE(
        nearestSpatialWindow(
            1, candidates.front().geometry, candidates, 1),
        std::optional<vkui::vk::WindowId>{2});
}

QTEST_GUILESS_MAIN(PanelTest)
#include "tst_panel.moc"
