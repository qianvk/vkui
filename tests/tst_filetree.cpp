// SPDX-License-Identifier: MIT

#include <QSignalSpy>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QtTest>
#include <type_traits>
#include <vkui/widgets/views/VkFileTreeView.h>

class FileTreeTest final : public QObject {
    Q_OBJECT

  private slots:
    void usesTheDisclosureTreeAsItsOnlyAnimationAuthority();
    void configuresCompactModelViewDefaults();
    void computesStableSharedGeometry();
};

void FileTreeTest::usesTheDisclosureTreeAsItsOnlyAnimationAuthority() {
    static_assert(std::is_base_of_v<vkui::VkDisclosureTreeView, vkui::VkFileTreeView>);

    QStandardItemModel model;
    auto* folder = new QStandardItem(QStringLiteral("Folder"));
    folder->setData(true, vkui::VkFileTreeDirectoryRole);
    folder->setData(QStringLiteral("/vault/Folder"), vkui::VkFileTreePathRole);
    auto* child = new QStandardItem(QStringLiteral("Chapter.txt"));
    child->setData(false, vkui::VkFileTreeDirectoryRole);
    child->setData(QStringLiteral("/vault/Folder/Chapter.txt"),
                   vkui::VkFileTreePathRole);
    folder->appendRow(child);
    model.appendRow(folder);

    vkui::VkFileTreeView tree;
    tree.resize(320, 240);
    tree.setModel(&model);
    tree.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tree));

    const QModelIndex folderIndex = model.index(0, 0);
    QSignalSpy expandedSpy(&tree, &QTreeView::expanded);
    QSignalSpy collapsedSpy(&tree, &QTreeView::collapsed);
    tree.setExpandedAnimated(folderIndex, true);
    QTest::qWait(35);
    tree.setExpandedAnimated(folderIndex, false);
    QTRY_VERIFY_WITH_TIMEOUT(!tree.isExpanded(folderIndex), 1000);
    QCOMPARE(expandedSpy.size(), 1);
    QCOMPARE(collapsedSpy.size(), 1);
}

void FileTreeTest::configuresCompactModelViewDefaults() {
    vkui::VkFileTreeView tree;
    QVERIFY(tree.header()->isHidden());
    QCOMPARE(tree.frameShape(), QFrame::NoFrame);
    QVERIFY(!tree.rootIsDecorated());
    QVERIFY(!tree.nativeBranchesVisible());
    QVERIFY(tree.uniformRowHeights());
    QVERIFY(!tree.expandsOnDoubleClick());
    QVERIFY(!tree.isAnimated());
    QVERIFY(qobject_cast<vkui::VkFileTreeDelegate*>(tree.itemDelegate()) != nullptr);
    QCOMPARE(tree.iconSize(), tree.fileTreeMetrics().glyphSlotSize);
    QCOMPARE(tree.indentation(), tree.fileTreeMetrics().glyphSlotSize.width()
                                    + tree.fileTreeMetrics().textGap);
}

void FileTreeTest::computesStableSharedGeometry() {
    vkui::VkFileTreeView tree;
    QStyleOptionViewItem option;
    option.initFrom(&tree);
    option.font = tree.font();
    option.fontMetrics = QFontMetrics(option.font);
    option.rect = QRect(24, 10, 260, 30);
    option.text = QStringLiteral("Chapter.txt");

    const vkui::VkTreeItemGeometry geometry = tree.fileTreeItemGeometry(option);
    QVERIFY(geometry.iconRect.isValid());
    QVERIFY(geometry.textRect.isValid());
    QVERIFY(geometry.pillRect.isValid());
    QCOMPARE(geometry.iconRect.center().y(), option.rect.center().y());
    QVERIFY(geometry.textRect.left() > geometry.iconRect.right());
    QVERIFY(geometry.pillRect.left() <= geometry.iconRect.left());
}

QTEST_MAIN(FileTreeTest)
#include "tst_filetree.moc"
