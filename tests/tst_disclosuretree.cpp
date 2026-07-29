// SPDX-License-Identifier: MIT

#include <QtTest>

#include <QStandardItemModel>
#include <QTimeLine>

#include <vkui/core/VkFileIcon.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/views/VkDisclosureTreeView.h>

class DisclosureTreeTest final : public QObject
{
    Q_OBJECT

private slots:
    void geometryBoundsIconTextAndPill();
    void disclosureUsesOneReversibleMovingSurface();
};

void DisclosureTreeTest::geometryBoundsIconTextAndPill()
{
    QStyleOptionViewItem option;
    option.rect = QRect(20, 10, 240, 28);
    option.font = QApplication::font();
    option.text = QStringLiteral("Chapter name");
    option.textElideMode = Qt::ElideRight;
    const vkui::VkFileIconMetrics metrics =
        vkui::fileIconMetrics(option.font, 1.0);

    const vkui::VkTreeItemGeometry geometry =
        vkui::treeItemGeometry(option, metrics);
    QVERIFY(option.rect.contains(geometry.iconRect));
    QVERIFY(option.rect.contains(geometry.textRect));
    QVERIFY(geometry.pillRect.contains(geometry.iconRect.center()));
    QVERIFY(geometry.pillRect.contains(geometry.textRect.center()));
    QVERIFY(geometry.pillRect.right() < option.rect.right());
}

void DisclosureTreeTest::
    disclosureUsesOneReversibleMovingSurface()
{
    auto *theme = vkui::VkThemeManager::instance();
    const bool originalAnimations =
        theme->animationsEnabled();
    theme->setAnimationsEnabled(true);

    QStandardItemModel model;
    auto *folder =
        new QStandardItem(QStringLiteral("Folder"));
    folder->appendRow(
        new QStandardItem(QStringLiteral("Child 1")));
    folder->appendRow(
        new QStandardItem(QStringLiteral("Child 2")));
    folder->appendRow(
        new QStandardItem(QStringLiteral("Child 3")));
    model.appendRow(folder);
    model.appendRow(
        new QStandardItem(QStringLiteral("Sibling")));

    vkui::VkDisclosureTreeView tree;
    tree.setModel(&model);
    tree.setUniformRowHeights(true);
    tree.setDisclosureSurfaceColor(Qt::white);
    QCOMPARE(
        tree.disclosureSurfaceColor(),
        QColor(Qt::white));
    tree.resize(320, 220);
    tree.show();
    QTest::qWait(20);

    const QModelIndex folderIndex =
        model.index(0, 0);
    QVERIFY(!tree.isExpanded(folderIndex));
    tree.setExpandedAnimated(folderIndex, true);
    QVERIFY(tree.isExpanded(folderIndex));

    QWidget *overlay =
        tree.viewport()->findChild<QWidget *>(
            QStringLiteral(
                "vkDisclosureGroupTransition"));
    QVERIFY(overlay != nullptr);
    QCOMPARE(
        overlay->property("surfaceCount").toInt(),
        1);
    QVERIFY(overlay->property("travel").toInt() > 0);
    QTRY_VERIFY(
        overlay->property("progress").toReal()
        > 0.0);
    QCOMPARE(
        overlay->property("seamGap").toInt(),
        0);
    QCOMPARE(
        overlay->property(
                   "branchRelativeSpan")
            .toInt(),
        overlay->property("travel").toInt());
    QCOMPARE(
        overlay->property("siblingOffset").toInt()
            - overlay->property("firstChildTop")
                  .toInt(),
        overlay->property("travel").toInt());

    // Popovers may adapt their content width while disclosure is running.
    // A horizontal-only resize must preserve the reversible surface.
    tree.resize(390, tree.height());
    QCoreApplication::processEvents();
    QCOMPARE(
        tree.viewport()
            ->findChildren<QWidget *>(
                QStringLiteral(
                    "vkDisclosureGroupTransition"))
            .size(),
        1);

    // A second click reverses the same time line and surface instead of
    // constructing an unrelated animation with a discontinuous curve.
    tree.setExpandedAnimated(folderIndex, false);
    QVERIFY(!tree.isExpanded(folderIndex));
    QCOMPARE(
        tree.viewport()
            ->findChildren<QWidget *>(
                QStringLiteral(
                    "vkDisclosureGroupTransition"))
            .size(),
        1);
    QTRY_VERIFY_WITH_TIMEOUT(
        tree.viewport()->findChild<QWidget *>(
            QStringLiteral(
                "vkDisclosureGroupTransition"))
            == nullptr,
        500);

    theme->setAnimationsEnabled(originalAnimations);
}

QTEST_MAIN(DisclosureTreeTest)
#include "tst_disclosuretree.moc"
