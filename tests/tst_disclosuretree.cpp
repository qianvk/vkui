// SPDX-License-Identifier: MIT

#include <QDir>
#include <QAbstractListModel>
#include <QPainter>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTimeLine>
#include <QVariantMap>
#include <QtTest>
#include <vkui/core/VkFileIcon.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/views/VkDisclosureTreeView.h>

namespace {

constexpr int VisualKindRole = Qt::UserRole + 73;
constexpr int ChildKind = 1;
constexpr int SiblingKind = 2;
constexpr int TrailingChildKind = 3;
const QColor ChildColor(40, 120, 220);
const QColor SiblingColor(220, 40, 40);
const QColor TrailingChildColor(40, 170, 90);

class SeamProbeDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return QSize(240, 28);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->fillRect(option.rect, QColor(Qt::white));
        const int kind = index.data(VisualKindRole).toInt();
        if (kind == ChildKind || kind == SiblingKind || kind == TrailingChildKind) {
            painter->fillRect(option.rect.adjusted(8, 3, -8, -3),
                              kind == ChildKind
                                  ? ChildColor
                                  : (kind == SiblingKind ? SiblingColor : TrailingChildColor));
        }
    }
};

class MoveListModel final : public QAbstractListModel {
  public:
    explicit MoveListModel(const int count, QObject* parent = nullptr) : QAbstractListModel(parent) {
        for (int row = 0; row < count; ++row) {
            m_rows.append(QStringLiteral("Row %1").arg(row));
        }
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }

    [[nodiscard]] QVariant data(const QModelIndex& index,
                                const int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
            return {};
        }
        if (role == Qt::DisplayRole) {
            return m_rows.at(index.row());
        }
        if (role == VisualKindRole) {
            return index.row() == 2 || index.row() == 3 ? ChildKind : SiblingKind;
        }
        return {};
    }

    bool moveRows(const QModelIndex& sourceParent, const int sourceRow, const int count,
                  const QModelIndex& destinationParent, const int destinationChild) override {
        if (sourceParent.isValid() || destinationParent.isValid() || count <= 0 || sourceRow < 0 ||
            sourceRow + count > m_rows.size() || destinationChild < 0 ||
            destinationChild > m_rows.size() ||
            (destinationChild >= sourceRow && destinationChild <= sourceRow + count)) {
            return false;
        }
        if (!beginMoveRows(sourceParent, sourceRow, sourceRow + count - 1, destinationParent,
                           destinationChild)) {
            return false;
        }
        QStringList moved;
        for (int index = 0; index < count; ++index) {
            moved.append(m_rows.takeAt(sourceRow));
        }
        const int insertion = destinationChild > sourceRow ? destinationChild - count
                                                            : destinationChild;
        for (int index = 0; index < moved.size(); ++index) {
            m_rows.insert(insertion + index, moved.at(index));
        }
        endMoveRows();
        return true;
    }

  private:
    QStringList m_rows;
};

class MutationListModel final : public QAbstractListModel {
  public:
    explicit MutationListModel(const int count, QObject* parent = nullptr)
        : QAbstractListModel(parent) {
        for (int row = 0; row < count; ++row) {
            m_rows.append({QStringLiteral("Existing %1").arg(row), SiblingKind});
        }
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }

    [[nodiscard]] QVariant data(const QModelIndex& index,
                                const int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
            return {};
        }
        if (role == Qt::DisplayRole) {
            return m_rows.at(index.row()).text;
        }
        if (role == VisualKindRole) {
            return m_rows.at(index.row()).kind;
        }
        return {};
    }

    bool insertRows(const int row, const int count, const QModelIndex& parent = {}) override {
        if (parent.isValid() || row < 0 || count <= 0 || row > m_rows.size()) {
            return false;
        }
        beginInsertRows(parent, row, row + count - 1);
        for (int offset = 0; offset < count; ++offset) {
            m_rows.insert(row + offset,
                          {QStringLiteral("Inserted %1").arg(offset), ChildKind});
        }
        endInsertRows();
        return true;
    }

    bool removeRows(const int row, const int count, const QModelIndex& parent = {}) override {
        if (parent.isValid() || row < 0 || count <= 0 || row + count > m_rows.size()) {
            return false;
        }
        beginRemoveRows(parent, row, row + count - 1);
        m_rows.remove(row, count);
        endRemoveRows();
        return true;
    }

  private:
    struct Row final {
        QString text;
        int kind = SiblingKind;
    };
    QVector<Row> m_rows;
};

struct ColorBand final {
    int first = -1;
    int last = -1;
};

[[nodiscard]] ColorBand colorBand(const QImage& image, const QColor& color) {
    const qreal ratio = image.devicePixelRatio();
    const int x = std::clamp(qRound(80.0 * ratio), 0, std::max(0, image.width() - 1));
    ColorBand result;
    for (int y = 0; y < image.height(); ++y) {
        if (image.pixelColor(x, y) != color) {
            continue;
        }
        if (result.first < 0) {
            result.first = y;
        }
        result.last = y;
    }
    return result;
}

} // namespace

class DisclosureTreeTest final : public QObject {
    Q_OBJECT

  private slots:
    void geometryBoundsIconTextAndPill();
    void compactBranchesKeepSelectionOutOfIndent();
    void disclosureUsesReversibleSharedBoundary();
    void everyFrameKeepsChildAndSiblingVisuallyJoined();
    void longBranchKeepsTrueTrailingChildAtSeam();
    void rowInsertionAndRemovalUseOneLocalTimeline();
    void rowMutationInterruptionStartsAtThePaintedFrame();
    void contiguousRowMoveUsesOneFrameClockAndBoundedCache();
    void ownedModelCanOutliveAnimationChildrenDuringViewTeardown();
};

void DisclosureTreeTest::geometryBoundsIconTextAndPill() {
    QStyleOptionViewItem option;
    option.rect = QRect(20, 10, 240, 28);
    option.font = QApplication::font();
    option.text = QStringLiteral("Chapter name");
    option.textElideMode = Qt::ElideRight;
    const vkui::VkFileIconMetrics metrics = vkui::fileIconMetrics(option.font, 1.0);

    const vkui::VkTreeItemGeometry geometry = vkui::treeItemGeometry(option, metrics);
    QVERIFY(option.rect.contains(geometry.iconRect));
    QVERIFY(option.rect.contains(geometry.textRect));
    QVERIFY(geometry.pillRect.contains(geometry.iconRect.center()));
    QVERIFY(geometry.pillRect.contains(geometry.textRect.center()));
    QVERIFY(option.rect.adjusted(2, 0, -2, 0).contains(geometry.pillRect));
    QVERIFY(geometry.pillRect.right() < option.rect.right());

    option.rect.moveLeft(0);
    const vkui::VkTreeItemGeometry rootGeometry = vkui::treeItemGeometry(option, metrics);
    QCOMPARE(rootGeometry.pillRect.left(), 2);
    QVERIFY(option.rect.adjusted(2, 0, -2, 0).contains(rootGeometry.pillRect));
}

void DisclosureTreeTest::compactBranchesKeepSelectionOutOfIndent() {
    QStandardItemModel model;
    auto* folder = new QStandardItem(QStringLiteral("Folder"));
    folder->appendRow(new QStandardItem(QStringLiteral("Child")));
    model.appendRow(folder);

    vkui::VkDisclosureTreeView tree;
    tree.setModel(&model);
    tree.setRootIsDecorated(false);
    tree.setUniformRowHeights(true);
    tree.setSelectionBehavior(QAbstractItemView::SelectItems);
    tree.setNativeBranchesVisible(false);
    tree.setDisclosureSurfaceColor(Qt::white);
    QPalette palette = tree.palette();
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::Window, Qt::white);
    palette.setColor(QPalette::Highlight, QColor(20, 100, 220));
    tree.setPalette(palette);
    tree.resize(320, 160);
    tree.expand(model.index(0, 0));
    tree.show();
    QTest::qWait(20);

    const QModelIndex child = model.index(0, 0, model.index(0, 0));
    const QRect childRect = tree.visualRect(child);
    QVERIFY(childRect.isValid());
    QVERIFY(childRect.left() > tree.viewport()->rect().left());
    tree.setCurrentIndex(child);
    QCoreApplication::processEvents();
    const QImage selected = tree.viewport()->grab().toImage();
    tree.clearSelection();
    tree.setCurrentIndex(QModelIndex());
    QCoreApplication::processEvents();
    const QImage plain = tree.viewport()->grab().toImage();

    const qreal ratio = selected.devicePixelRatio();
    const int left = qRound(tree.viewport()->rect().left() * ratio);
    const int right = qRound(childRect.left() * ratio) - 1;
    const int top = qRound(childRect.top() * ratio);
    const int bottom = qRound((childRect.bottom() + 1) * ratio) - 1;
    QVERIFY(right >= left);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            QCOMPARE(selected.pixelColor(x, y), plain.pixelColor(x, y));
            QCOMPARE(plain.pixelColor(x, y), QColor(Qt::white));
        }
    }
}

void DisclosureTreeTest::disclosureUsesReversibleSharedBoundary() {
    auto* theme = vkui::VkThemeManager::instance();
    const bool originalAnimations = theme->animationsEnabled();
    theme->setAnimationsEnabled(true);

    QStandardItemModel model;
    auto* folder = new QStandardItem(QStringLiteral("Folder"));
    folder->appendRow(new QStandardItem(QStringLiteral("Child 1")));
    folder->appendRow(new QStandardItem(QStringLiteral("Child 2")));
    folder->appendRow(new QStandardItem(QStringLiteral("Child 3")));
    model.appendRow(folder);
    model.appendRow(new QStandardItem(QStringLiteral("Sibling")));

    vkui::VkDisclosureTreeView tree;
    tree.setModel(&model);
    tree.setUniformRowHeights(true);
    tree.setDisclosureSurfaceColor(Qt::white);
    QCOMPARE(tree.disclosureSurfaceColor(), QColor(Qt::white));
    tree.resize(320, 220);
    tree.show();
    QTest::qWait(20);

    const QModelIndex folderIndex = model.index(0, 0);
    QVERIFY(!tree.isExpanded(folderIndex));
    tree.setExpandedAnimated(folderIndex, true);
    QVERIFY(tree.isExpanded(folderIndex));

    QWidget* overlay =
        tree.viewport()->findChild<QWidget*>(QStringLiteral("vkDisclosureGroupTransition"));
    QVERIFY(overlay != nullptr);
    QCOMPARE(overlay->property("surfaceCount").toInt(), 2);
    QVERIFY(overlay->property("travel").toInt() > 0);
    QTRY_VERIFY(overlay->property("progress").toReal() > 0.0);
    QCOMPARE(overlay->property("seamGap").toInt(), 0);
    QCOMPARE(overlay->property("branchRelativeSpan").toInt(), overlay->property("travel").toInt());
    QCOMPARE(overlay->property("siblingOffset").toInt() -
                 overlay->property("firstChildTop").toInt(),
             overlay->property("travel").toInt());

    // Popovers may adapt their content width while disclosure is running.
    // A horizontal-only resize must preserve the reversible surface.
    tree.resize(390, tree.height());
    QCoreApplication::processEvents();
    QCOMPARE(tree.viewport()
                 ->findChildren<QWidget*>(QStringLiteral("vkDisclosureGroupTransition"))
                 .size(),
             1);

    // A second click reverses the same time line and surface instead of
    // constructing an unrelated animation with a discontinuous curve.
    tree.setExpandedAnimated(folderIndex, false);
    QVERIFY(!tree.isExpanded(folderIndex));
    QCOMPARE(tree.viewport()
                 ->findChildren<QWidget*>(QStringLiteral("vkDisclosureGroupTransition"))
                 .size(),
             1);
    QTRY_VERIFY_WITH_TIMEOUT(tree.viewport()->findChild<QWidget*>(
                                 QStringLiteral("vkDisclosureGroupTransition")) == nullptr,
                             500);

    theme->setAnimationsEnabled(originalAnimations);
}

void DisclosureTreeTest::everyFrameKeepsChildAndSiblingVisuallyJoined() {
    auto* theme = vkui::VkThemeManager::instance();
    const bool originalAnimations = theme->animationsEnabled();
    theme->setAnimationsEnabled(true);

    QStandardItemModel model;
    auto* folder = new QStandardItem(QStringLiteral("Folder"));
    for (int row = 0; row < 2; ++row) {
        auto* child = new QStandardItem(QStringLiteral("Child %1").arg(row));
        child->setData(ChildKind, VisualKindRole);
        folder->appendRow(child);
    }
    auto* sibling = new QStandardItem(QStringLiteral("Sibling"));
    sibling->setData(SiblingKind, VisualKindRole);
    model.appendRow(folder);
    model.appendRow(sibling);

    vkui::VkDisclosureTreeView tree;
    tree.setModel(&model);
    tree.setItemDelegate(new SeamProbeDelegate(&tree));
    tree.setUniformRowHeights(true);
    tree.setDisclosureSurfaceColor(Qt::white);
    tree.resize(320, 220);
    tree.show();
    QTest::qWait(20);

    tree.setExpandedAnimated(model.index(0, 0), true);
    auto* overlay =
        tree.viewport()->findChild<QWidget*>(QStringLiteral("vkDisclosureGroupTransition"));
    auto* timeline = tree.findChild<QTimeLine*>(QStringLiteral("vkDisclosureTimeline"));
    QVERIFY(overlay != nullptr);
    QVERIFY(timeline != nullptr);
    timeline->setPaused(true);

    int firstChildFrame = -1;
    const QString frameDirectory = qEnvironmentVariable("VKUI_DISCLOSURE_FRAME_DIRECTORY");
    if (!frameDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(frameDirectory));
    }
    QVector<QImage> forwardFrames;
    for (int time = 0; time <= timeline->duration(); time += 8) {
        timeline->setCurrentTime(time);
        QCoreApplication::processEvents();
        const QImage frame = overlay->grab().toImage();
        forwardFrames.append(frame);
        if (!frameDirectory.isEmpty()) {
            QVERIFY(frame.save(
                QDir(frameDirectory)
                    .filePath(QStringLiteral("frame-%1.png").arg(time, 3, 10, QLatin1Char('0')))));
        }
        const ColorBand child = colorBand(frame, ChildColor);
        const ColorBand following = colorBand(frame, SiblingColor);
        const int siblingOffset = overlay->property("siblingOffset").toInt();
        QCOMPARE(overlay->property("lastChildBottom").toInt(), siblingOffset);
        QCOMPARE(overlay->property("followingTop").toInt(), siblingOffset);
        QCOMPARE(overlay->property("seamGap").toInt(), 0);
        QVERIFY2(following.first >= 0,
                 qPrintable(QStringLiteral("Sibling missing at %1 ms").arg(time)));
        // At the first physical pixel the trailing row's bottom padding can
        // be visible before its colored probe. Geometry must still move; do
        // not quantize the animation to the first painted glyph.
        QVERIFY2(siblingOffset <= 2 || child.first >= 0,
                 qPrintable(QStringLiteral("Sibling moved %1 px before a "
                                           "child became visible at %2 ms")
                                .arg(siblingOffset)
                                .arg(time)));
        if (child.first < 0) {
            continue;
        }
        if (firstChildFrame < 0) {
            firstChildFrame = time;
        }
        const int normalContentGap = qRound(6.0 * std::max(1.0, frame.devicePixelRatio()));
        QVERIFY2(following.first - child.last - 1 <= normalContentGap,
                 qPrintable(QStringLiteral("Visual seam grew at %1 ms: "
                                           "childLast=%2 siblingFirst=%3")
                                .arg(time)
                                .arg(child.last)
                                .arg(following.first)));
    }
    // At 120 Hz, a short branch must expose child content by the second
    // rendered frame instead of spending tens of milliseconds on blank rows.
    QVERIFY2(
        firstChildFrame >= 0 && firstChildFrame <= 16,
        qPrintable(QStringLiteral("First child was delayed until %1 ms").arg(firstChildFrame)));

    timeline->setDirection(QTimeLine::Backward);
    for (int time = timeline->duration(); time >= 0; time -= 8) {
        timeline->setCurrentTime(time);
        QCoreApplication::processEvents();
        const QImage reverseFrame = overlay->grab().toImage();
        const int forwardIndex = time / 8;
        QVERIFY2(forwardIndex >= 0 && forwardIndex < forwardFrames.size(),
                 "Missing corresponding forward frame");
        QVERIFY2(reverseFrame == forwardFrames.at(forwardIndex),
                 qPrintable(QStringLiteral("Reverse path diverged at %1 ms").arg(time)));
    }

    tree.finishDisclosureAnimation();
    theme->setAnimationsEnabled(originalAnimations);
}

void DisclosureTreeTest::longBranchKeepsTrueTrailingChildAtSeam() {
    auto* theme = vkui::VkThemeManager::instance();
    const bool originalAnimations = theme->animationsEnabled();
    theme->setAnimationsEnabled(true);

    QStandardItemModel model;
    auto* folder = new QStandardItem(QStringLiteral("第一卷"));
    for (int row = 0; row < 120; ++row) {
        auto* chapter = new QStandardItem(QStringLiteral("第%1章").arg(row + 1));
        chapter->setData(row == 119 ? TrailingChildKind : ChildKind, VisualKindRole);
        folder->appendRow(chapter);
    }
    auto* nextVolume = new QStandardItem(QStringLiteral("第二卷"));
    nextVolume->setData(SiblingKind, VisualKindRole);
    model.appendRow(folder);
    model.appendRow(nextVolume);

    vkui::VkDisclosureTreeView tree;
    tree.setModel(&model);
    tree.setItemDelegate(new SeamProbeDelegate(&tree));
    tree.setUniformRowHeights(true);
    tree.setDisclosureSurfaceColor(Qt::white);
    tree.resize(320, 220);
    tree.show();
    QTest::qWait(20);
    tree.setCurrentIndex(model.index(1, 0));
    const int collapsedScrollMaximum = tree.verticalScrollBar()->maximum();
    tree.setExpandedAnimated(model.index(0, 0), true);

    auto* overlay =
        tree.viewport()->findChild<QWidget*>(QStringLiteral("vkDisclosureGroupTransition"));
    auto* timeline = tree.findChild<QTimeLine*>(QStringLiteral("vkDisclosureTimeline"));
    QVERIFY(overlay != nullptr);
    QVERIFY(timeline != nullptr);
    timeline->setPaused(true);
    const int totalTravel = overlay->property("totalTravel").toInt();
    const int visualTravel = overlay->property("visualTravel").toInt();
    QCOMPARE(visualTravel, totalTravel);
    QVERIFY(totalTravel > overlay->height());
    QCOMPARE(overlay->property("trailingItemText").toString(), QStringLiteral("第120章"));
    QCOMPARE(overlay->property("followingItemText").toString(), QStringLiteral("第二卷"));
    QCOMPARE(overlay->property("collapsedScrollMaximum").toInt(), collapsedScrollMaximum);
    const int expandedScrollMaximum = overlay->property("expandedScrollMaximum").toInt();
    QVERIFY(expandedScrollMaximum > collapsedScrollMaximum);
    QCOMPARE(tree.verticalScrollBar()->maximum(), collapsedScrollMaximum);

    int previousOffset = -1;
    int previousScrollMaximum = collapsedScrollMaximum;
    const QString frameDirectory = qEnvironmentVariable("VKUI_DISCLOSURE_FRAME_DIRECTORY");
    if (!frameDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(frameDirectory));
    }
    for (int time = 0; time <= timeline->duration(); time += 8) {
        timeline->setCurrentTime(time);
        QCoreApplication::processEvents();
        const int offset = overlay->property("siblingOffset").toInt();
        QCOMPARE(overlay->property("followingTop").toInt(), offset);
        QVERIFY(offset >= previousOffset);
        QVERIFY(offset >= 0);
        QVERIFY(offset <= visualTravel);
        previousOffset = offset;
        const qreal progress = overlay->property("progress").toReal();
        const int expectedScrollMaximum =
            collapsedScrollMaximum +
            qRound(progress * (expandedScrollMaximum - collapsedScrollMaximum));
        QCOMPARE(tree.verticalScrollBar()->maximum(), expectedScrollMaximum);
        QVERIFY(tree.verticalScrollBar()->maximum() >= previousScrollMaximum);
        previousScrollMaximum = tree.verticalScrollBar()->maximum();

        const QImage frame = overlay->grab().toImage();
        if (!frameDirectory.isEmpty()) {
            QVERIFY(frame.save(
                QDir(frameDirectory)
                    .filePath(
                        QStringLiteral("long-frame-%1.png").arg(time, 3, 10, QLatin1Char('0')))));
            QVERIFY(tree.viewport()->grab().save(
                QDir(frameDirectory)
                    .filePath(QStringLiteral("long-viewport-frame-%1.png")
                                  .arg(time, 3, 10, QLatin1Char('0')))));
        }
        const ColorBand child = colorBand(frame, ChildColor);
        const ColorBand trailingChild = colorBand(frame, TrailingChildColor);
        const ColorBand sibling = colorBand(frame, SiblingColor);
        if (offset > 0) {
            QVERIFY2(child.first >= 0 || trailingChild.first >= 0,
                     qPrintable(QStringLiteral("Long branch left a blank "
                                               "frame at %1 ms")
                                    .arg(time)));
        }
        if (offset > 0 && sibling.first >= 0) {
            QVERIFY2(trailingChild.first >= 0,
                     qPrintable(QStringLiteral("True trailing child missing at %1 ms; sibling=%2")
                                    .arg(time)
                                    .arg(sibling.first)));
            const int normalGap = qRound(6.0 * std::max(1.0, frame.devicePixelRatio()));
            QVERIFY2(sibling.first - trailingChild.last - 1 <= normalGap,
                     qPrintable(QStringLiteral("Long branch seam opened "
                                               "at %1 ms: childLast=%2 "
                                               "siblingFirst=%3 offset=%4")
                                    .arg(time)
                                    .arg(trailingChild.last)
                                    .arg(sibling.first)
                                    .arg(offset)));
        }
        if (time == 8) {
            QVERIFY(offset > 0);
            QCOMPARE(overlay->property("lastVisibleChildText").toString(),
                     QStringLiteral("第120章"));
            QVERIFY(offset <= 56);
            QVERIFY(tree.verticalScrollBar()->maximum() < expandedScrollMaximum);
        }
        if (time == 64) {
            tree.verticalScrollBar()->setValue(std::min(tree.verticalScrollBar()->maximum(), 80));
            QCoreApplication::processEvents();
            QCOMPARE(tree.verticalScrollBar()->value(), 0);
            QCOMPARE(
                tree.viewport()->findChild<QWidget*>(QStringLiteral("vkDisclosureGroupTransition")),
                overlay);
        }
    }

    tree.finishDisclosureAnimation();
    theme->setAnimationsEnabled(originalAnimations);
}

void DisclosureTreeTest::rowInsertionAndRemovalUseOneLocalTimeline() {
    auto* theme = vkui::VkThemeManager::instance();
    const bool originalAnimations = theme->animationsEnabled();
    theme->setAnimationsEnabled(true);

    MutationListModel model(36);
    vkui::VkDisclosureTreeView tree;
    tree.setModel(&model);
    tree.setItemDelegate(new SeamProbeDelegate(&tree));
    tree.setUniformRowHeights(true);
    tree.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree.setDisclosureSurfaceColor(Qt::white);
    tree.resize(320, 224);
    tree.show();
    QTest::qWait(20);

    const int oldMaximum = tree.verticalScrollBar()->maximum();
    QVERIFY(model.insertRows(2, 3));

    auto* overlay =
        tree.viewport()->findChild<QWidget*>(QStringLiteral("vkTreeMutationTransition"));
    auto* timeline = tree.findChild<QTimeLine*>(QStringLiteral("vkTreeMutationTimeline"));
    QVERIFY(overlay != nullptr);
    QVERIFY(timeline != nullptr);
    timeline->setPaused(true);
    QCOMPARE(overlay->property("mutationKind").toString(), QStringLiteral("insert"));
    QVERIFY(overlay->geometry().top() > 0);
    QVERIFY(overlay->geometry().height() < tree.viewport()->height());
    QVERIFY(overlay->property("surfaceArea").toInt() <
            overlay->property("viewportArea").toInt());
    QCOMPARE(overlay->property("startScrollMaximum").toInt(), oldMaximum);
    const int insertedMaximum = overlay->property("targetScrollMaximum").toInt();
    QVERIFY(insertedMaximum > oldMaximum);
    const QString mutationFrameDirectory =
        qEnvironmentVariable("VKUI_MUTATION_FRAME_DIRECTORY");
    if (!mutationFrameDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(mutationFrameDirectory));
    }

    int previousMaximum = oldMaximum;
    for (int time = 0; time <= timeline->duration(); time += 8) {
        timeline->setCurrentTime(time);
        QCoreApplication::processEvents();
        QCOMPARE(overlay->property("seamGap").toInt(), 0);
        const qreal progress = overlay->property("progress").toReal();
        const int expected =
            oldMaximum + qRound(progress * (insertedMaximum - oldMaximum));
        QCOMPARE(tree.verticalScrollBar()->maximum(), expected);
        QVERIFY(tree.verticalScrollBar()->maximum() >= previousMaximum);
        previousMaximum = tree.verticalScrollBar()->maximum();
        if (!mutationFrameDirectory.isEmpty() &&
            (time == 0 || time == 96 || time == timeline->duration())) {
            QVERIFY(tree.viewport()->grab().save(
                QDir(mutationFrameDirectory)
                    .filePath(QStringLiteral("insert-%1.png")
                                  .arg(time, 3, 10, QLatin1Char('0')))));
        }
    }
    tree.finishRowMutationAnimation();

    const int beforeRemovalMaximum = tree.verticalScrollBar()->maximum();
    QVERIFY(model.removeRows(2, 3));
    overlay = tree.viewport()->findChild<QWidget*>(QStringLiteral("vkTreeMutationTransition"));
    QVERIFY(overlay != nullptr);
    timeline->setPaused(true);
    QCOMPARE(overlay->property("mutationKind").toString(), QStringLiteral("remove"));
    const int removedMaximum = overlay->property("targetScrollMaximum").toInt();
    QVERIFY(removedMaximum < beforeRemovalMaximum);
    previousMaximum = beforeRemovalMaximum;
    for (int time = 0; time <= timeline->duration(); time += 8) {
        timeline->setCurrentTime(time);
        QCoreApplication::processEvents();
        QCOMPARE(overlay->property("seamGap").toInt(), 0);
        QVERIFY(tree.verticalScrollBar()->maximum() <= previousMaximum);
        previousMaximum = tree.verticalScrollBar()->maximum();
    }
    tree.finishRowMutationAnimation();
    theme->setAnimationsEnabled(originalAnimations);
}

void DisclosureTreeTest::rowMutationInterruptionStartsAtThePaintedFrame() {
    auto* theme = vkui::VkThemeManager::instance();
    const bool originalAnimations = theme->animationsEnabled();
    theme->setAnimationsEnabled(true);

    QStandardItemModel model;
    for (int row = 0; row < 18; ++row) {
        model.appendRow(new QStandardItem(QStringLiteral("Row %1").arg(row)));
    }
    vkui::VkDisclosureTreeView tree;
    tree.setModel(&model);
    tree.setUniformRowHeights(true);
    tree.setDisclosureSurfaceColor(Qt::white);
    tree.resize(320, 224);
    tree.show();
    QTest::qWait(20);

    model.insertRow(2, new QStandardItem(QStringLiteral("First mutation")));
    auto* timeline = tree.findChild<QTimeLine*>(QStringLiteral("vkTreeMutationTimeline"));
    QVERIFY(timeline != nullptr);
    timeline->setPaused(true);
    timeline->setCurrentTime(72);
    QCoreApplication::processEvents();
    auto* interrupted =
        tree.viewport()->findChild<QWidget*>(QStringLiteral("vkTreeMutationTransition"));
    QVERIFY(interrupted != nullptr);
    const QVariantMap geometryBeforeInterruption =
        interrupted->property("currentGeometry").toMap();
    const QString mutationFrameDirectory =
        qEnvironmentVariable("VKUI_MUTATION_FRAME_DIRECTORY");
    if (!mutationFrameDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(mutationFrameDirectory));
        QVERIFY(tree.viewport()->grab().save(
            QDir(mutationFrameDirectory)
                .filePath(QStringLiteral("interrupted-before.png"))));
    }

    model.insertRow(2, new QStandardItem(QStringLiteral("Second mutation")));
    auto* replacement =
        tree.viewport()->findChild<QWidget*>(QStringLiteral("vkTreeMutationTransition"));
    QVERIFY(replacement != nullptr);
    timeline->setPaused(true);
    timeline->setCurrentTime(0);
    QCoreApplication::processEvents();
    const QVariantMap geometryAfterInterruption =
        replacement->property("currentGeometry").toMap();
    if (!mutationFrameDirectory.isEmpty()) {
        QVERIFY(tree.viewport()->grab().save(
            QDir(mutationFrameDirectory)
                .filePath(QStringLiteral("interrupted-after.png"))));
    }
    for (auto row = geometryBeforeInterruption.cbegin();
         row != geometryBeforeInterruption.cend(); ++row) {
        if (!row.value().toRect().intersects(tree.viewport()->rect())) {
            continue;
        }
        QVERIFY2(geometryAfterInterruption.contains(row.key()), qPrintable(row.key()));
        QCOMPARE(geometryAfterInterruption.value(row.key()).toRect(), row.value().toRect());
    }

    tree.finishRowMutationAnimation();
    theme->setAnimationsEnabled(originalAnimations);
}

void DisclosureTreeTest::contiguousRowMoveUsesOneFrameClockAndBoundedCache() {
    auto* theme = vkui::VkThemeManager::instance();
    const bool originalAnimations = theme->animationsEnabled();
    theme->setAnimationsEnabled(true);

    MoveListModel model(30);
    vkui::VkDisclosureTreeView tree;
    tree.setModel(&model);
    tree.setItemDelegate(new SeamProbeDelegate(&tree));
    tree.setUniformRowHeights(true);
    tree.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree.setDisclosureSurfaceColor(Qt::white);
    tree.resize(320, 336);
    tree.show();
    QTest::qWait(20);

    QVERIFY(model.moveRows({}, 2, 2, {}, 9));
    auto* overlay =
        tree.viewport()->findChild<QWidget*>(QStringLiteral("vkTreeMutationTransition"));
    auto* timeline = tree.findChild<QTimeLine*>(QStringLiteral("vkTreeMutationTimeline"));
    QVERIFY(overlay != nullptr);
    QVERIFY(timeline != nullptr);
    timeline->setPaused(true);
    QCOMPARE(overlay->property("mutationKind").toString(), QStringLiteral("move"));

    const QVariantMap from = overlay->property("fromGeometry").toMap();
    const QVariantMap to = overlay->property("toGeometry").toMap();
    for (const QString& key : {QStringLiteral("Row 2"), QStringLiteral("Row 3"),
                               QStringLiteral("Row 4")}) {
        QVERIFY2(from.contains(key), qPrintable(key));
        QVERIFY2(to.contains(key), qPrintable(key));
    }
    const int rowHeight = from.value(QStringLiteral("Row 3")).toRect().top() -
                          from.value(QStringLiteral("Row 2")).toRect().top();
    QVERIFY(rowHeight > 0);
    QCOMPARE(to.value(QStringLiteral("Row 3")).toRect().top() -
                 to.value(QStringLiteral("Row 2")).toRect().top(),
             rowHeight);
    const int visibleBound = qCeil(static_cast<qreal>(tree.viewport()->height()) / rowHeight) + 4;
    QVERIFY(overlay->property("rowCount").toInt() <= visibleBound);
    QVERIFY(overlay->property("cachedPixelHeight").toInt() <=
            qCeil(visibleBound * rowHeight * tree.devicePixelRatioF()));

    const int startMaximum = overlay->property("startScrollMaximum").toInt();
    const int targetMaximum = overlay->property("targetScrollMaximum").toInt();
    int previousMaximum = startMaximum;
    for (int time = 0; time <= timeline->duration(); time += 8) {
        timeline->setCurrentTime(time);
        QCoreApplication::processEvents();
        const qreal progress = overlay->property("progress").toReal();
        const QVariantMap current = overlay->property("currentGeometry").toMap();
        for (const QString& key : {QStringLiteral("Row 2"), QStringLiteral("Row 3"),
                                   QStringLiteral("Row 4")}) {
            const QRect start = from.value(key).toRect();
            const QRect finish = to.value(key).toRect();
            const int expectedY = qRound(start.top() + (finish.top() - start.top()) * progress);
            QCOMPARE(current.value(key).toRect().top(), expectedY);
        }
        QCOMPARE(current.value(QStringLiteral("Row 3")).toRect().top() -
                     current.value(QStringLiteral("Row 2")).toRect().top(),
                 rowHeight);
        QCOMPARE(overlay->property("seamGap").toInt(), 0);
        const int expectedMaximum =
            startMaximum + qRound(progress * (targetMaximum - startMaximum));
        QCOMPARE(tree.verticalScrollBar()->maximum(), expectedMaximum);
        if (targetMaximum >= startMaximum) {
            QVERIFY(tree.verticalScrollBar()->maximum() >= previousMaximum);
        } else {
            QVERIFY(tree.verticalScrollBar()->maximum() <= previousMaximum);
        }
        previousMaximum = tree.verticalScrollBar()->maximum();
    }
    QCOMPARE(tree.verticalScrollBar()->maximum(), targetMaximum);
    tree.finishRowMutationAnimation();
    theme->setAnimationsEnabled(originalAnimations);
}

void DisclosureTreeTest::ownedModelCanOutliveAnimationChildrenDuringViewTeardown() {
    auto* theme = vkui::VkThemeManager::instance();
    const bool originalAnimations = theme->animationsEnabled();
    theme->setAnimationsEnabled(true);

    auto* tree = new vkui::VkDisclosureTreeView;
    auto* model = new QStandardItemModel(tree);
    auto* folder = new QStandardItem(QStringLiteral("Folder"));
    folder->appendRow(new QStandardItem(QStringLiteral("Child")));
    model->appendRow(folder);
    model->appendRow(new QStandardItem(QStringLiteral("Sibling")));
    tree->setModel(model);
    tree->resize(320, 180);
    tree->show();
    QTest::qWait(20);
    tree->setExpandedAnimated(model->index(0, 0), true);
    model->insertRow(1, new QStandardItem(QStringLiteral("Inserted")));
    QVERIFY(tree->findChild<QTimeLine*>(QStringLiteral("vkDisclosureTimeline")) != nullptr);
    QVERIFY(tree->findChild<QTimeLine*>(QStringLiteral("vkTreeMutationTimeline")) != nullptr);

    // Regression: child destruction used to let model::destroyed call back
    // after the timeline child was already gone, dereferencing a stale raw
    // pointer from finishDisclosureAnimation().
    delete tree;
    theme->setAnimationsEnabled(originalAnimations);
}

QTEST_MAIN(DisclosureTreeTest)
#include "tst_disclosuretree.moc"
