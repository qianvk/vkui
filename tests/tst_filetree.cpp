// SPDX-License-Identifier: MIT

#include <QHeaderView>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QtTest>
#include <type_traits>
#include <vkui/widgets/views/VFileTreeView.h>

namespace {

class SemanticLeadingTextDelegate final : public vkui::VFileTreeDelegate {
  public:
    using vkui::VFileTreeDelegate::VFileTreeDelegate;

  protected:
    [[nodiscard]] vkui::VFileTreeRowPresentation
    fileTreePresentation(const QModelIndex&) const override {
        vkui::VFileTreeRowPresentation presentation;
        presentation.leadingText = QStringLiteral("H2");
        return presentation;
    }
};

} // namespace

class FileTreeTest final : public QObject {
    Q_OBJECT

  private slots:
    void usesTreeViewAsItsOnlyAnimationAuthority();
    void configuresCompactModelViewDefaults();
    void computesStableSharedGeometry();
    void semanticLeadingTextUsesTheSharedRowRenderer();
};

void FileTreeTest::usesTreeViewAsItsOnlyAnimationAuthority() {
    static_assert(std::is_base_of_v<vkui::VTreeView, vkui::VFileTreeView>);

    QStandardItemModel model;
    auto* folder = new QStandardItem(QStringLiteral("Folder"));
    folder->setData(true, vkui::VFileTreeDirectoryRole);
    folder->setData(QStringLiteral("/vault/Folder"), vkui::VFileTreePathRole);
    auto* child = new QStandardItem(QStringLiteral("Chapter.txt"));
    child->setData(false, vkui::VFileTreeDirectoryRole);
    child->setData(QStringLiteral("/vault/Folder/Chapter.txt"), vkui::VFileTreePathRole);
    folder->appendRow(child);
    model.appendRow(folder);

    vkui::VFileTreeView tree;
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
    vkui::VFileTreeView tree;
    QVERIFY(tree.header()->isHidden());
    QCOMPARE(tree.frameShape(), QFrame::NoFrame);
    QVERIFY(!tree.rootIsDecorated());
    QVERIFY(tree.branchLinesVisible());
    QVERIFY(tree.uniformRowHeights());
    QVERIFY(!tree.expandsOnDoubleClick());
    QVERIFY(!tree.isAnimated());
    QVERIFY(qobject_cast<vkui::VFileTreeDelegate*>(tree.itemDelegate()) != nullptr);
    QCOMPARE(tree.treeItemDelegate(), tree.itemDelegate());
    QCOMPARE(tree.iconSize(), tree.fileTreeMetrics().iconSize);
    QCOMPARE(tree.indentation(),
             tree.fileTreeMetrics().iconSize.width() + tree.fileTreeMetrics().textGap);
}

void FileTreeTest::computesStableSharedGeometry() {
    QStandardItemModel model;
    auto* item = new QStandardItem(QStringLiteral("Chapter.txt"));
    item->setData(QStringLiteral("Chapter.txt"), vkui::VFileTreePathRole);
    model.appendRow(item);

    vkui::VFileTreeView tree;
    tree.resize(320, 90);
    tree.setModel(&model);
    tree.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tree));

    const QModelIndex index = model.index(0, 0);
    const vkui::VTreeItemLayout layout = tree.itemLayout(index);
    QVERIFY(layout.leadingRect.isValid());
    QVERIFY(layout.textRect.isValid());
    QVERIFY(layout.backgroundRect.isValid());
    QCOMPARE(layout.leadingRect, tree.fileTreeIconRect(index));
    QCOMPARE(layout.leadingRect.center().y(), tree.visualRect(index).center().y());
    QVERIFY(layout.textRect.left() > layout.leadingRect.right());
    QVERIFY(layout.backgroundRect.contains(layout.leadingRect.center()));
}

void FileTreeTest::semanticLeadingTextUsesTheSharedRowRenderer() {
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("Chapter")));

    vkui::VFileTreeView tree;
    tree.resize(320, 90);
    tree.setItemDelegate(new SemanticLeadingTextDelegate(&tree, &tree));
    tree.setModel(&model);
    tree.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tree));
    QCoreApplication::processEvents();

    const QModelIndex first = model.index(0, 0);
    QCOMPARE(tree.visualRect(first).top(), 0);
    const QRect rowRect = tree.visualRect(first);
    const QRect iconRect = tree.fileTreeIconRect(first);
    QVERIFY(rowRect.contains(iconRect));
    const QImage icon = tree.viewport()->grab(iconRect).toImage();
    QVERIFY(!icon.isNull());
    const QColor surface = tree.viewport()->palette().color(QPalette::Base);
    int nonSurfacePixels = 0;
    for (int y = 0; y < icon.height(); ++y) {
        for (int x = 0; x < icon.width(); ++x) {
            nonSurfacePixels += icon.pixelColor(x, y) == surface ? 0 : 1;
        }
    }
    QVERIFY(nonSurfacePixels > 4);
}

QTEST_MAIN(FileTreeTest)
#include "tst_filetree.moc"
