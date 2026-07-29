// SPDX-License-Identifier: MIT

#include <QDir>
#include <QPainter>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTimeLine>
#include <QtTest>
#include <vkui/core/VkFileIcon.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/views/VkDisclosureTreeView.h>

namespace {

constexpr int VisualKindRole = Qt::UserRole + 73;
constexpr int ChildKind = 1;
constexpr int SiblingKind = 2;
const QColor ChildColor(40, 120, 220);
const QColor SiblingColor(220, 40, 40);

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
        if (kind == ChildKind || kind == SiblingKind) {
            painter->fillRect(option.rect.adjusted(8, 3, -8, -3),
                              kind == ChildKind ? ChildColor : SiblingColor);
        }
    }
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
    void disclosureUsesReversibleSharedBoundary();
    void everyFrameKeepsChildAndSiblingVisuallyJoined();
    void longBranchUsesViewportBoundedReveal();
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
    QVERIFY(geometry.pillRect.right() < option.rect.right());
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
        QVERIFY2(following.first >= 0,
                 qPrintable(QStringLiteral("Sibling missing at %1 ms").arg(time)));
        QVERIFY2(siblingOffset <= 0 || child.first >= 0,
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

void DisclosureTreeTest::longBranchUsesViewportBoundedReveal() {
    auto* theme = vkui::VkThemeManager::instance();
    const bool originalAnimations = theme->animationsEnabled();
    theme->setAnimationsEnabled(true);

    QStandardItemModel model;
    auto* folder = new QStandardItem(QStringLiteral("第一卷"));
    for (int row = 0; row < 120; ++row) {
        auto* chapter = new QStandardItem(QStringLiteral("第%1章").arg(row + 1));
        chapter->setData(ChildKind, VisualKindRole);
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
    tree.setExpandedAnimated(model.index(0, 0), true);

    auto* overlay =
        tree.viewport()->findChild<QWidget*>(QStringLiteral("vkDisclosureGroupTransition"));
    auto* timeline = tree.findChild<QTimeLine*>(QStringLiteral("vkDisclosureTimeline"));
    QVERIFY(overlay != nullptr);
    QVERIFY(timeline != nullptr);
    timeline->setPaused(true);
    const int anchoredScrollValue = tree.verticalScrollBar()->value();
    tree.verticalScrollBar()->setValue(
        std::min(tree.verticalScrollBar()->maximum(), anchoredScrollValue + 80));
    QCoreApplication::processEvents();
    QCOMPARE(tree.verticalScrollBar()->value(), anchoredScrollValue);
    QCOMPARE(tree.viewport()->findChild<QWidget*>(QStringLiteral("vkDisclosureGroupTransition")),
             overlay);
    const int totalTravel = overlay->property("totalTravel").toInt();
    const int visualTravel = overlay->property("visualTravel").toInt();
    QVERIFY(totalTravel > visualTravel);
    QVERIFY(visualTravel >= overlay->height());
    QVERIFY(visualTravel < overlay->height() + 28);

    int previousOffset = -1;
    const QString frameDirectory = qEnvironmentVariable("VKUI_DISCLOSURE_FRAME_DIRECTORY");
    if (!frameDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(frameDirectory));
    }
    for (int time = 0; time <= timeline->duration(); time += 8) {
        timeline->setCurrentTime(time);
        QCoreApplication::processEvents();
        const int offset = overlay->property("siblingOffset").toInt();
        QVERIFY(offset >= previousOffset);
        QVERIFY(offset >= 0);
        QVERIFY(offset <= visualTravel);
        previousOffset = offset;

        const QImage frame = overlay->grab().toImage();
        if (!frameDirectory.isEmpty()) {
            QVERIFY(frame.save(
                QDir(frameDirectory)
                    .filePath(
                        QStringLiteral("long-frame-%1.png").arg(time, 3, 10, QLatin1Char('0')))));
        }
        const ColorBand child = colorBand(frame, ChildColor);
        const ColorBand sibling = colorBand(frame, SiblingColor);
        if (offset > 0) {
            QVERIFY2(child.first >= 0, qPrintable(QStringLiteral("Long branch left a blank "
                                                                 "frame at %1 ms")
                                                      .arg(time)));
        }
        if (child.first >= 0 && sibling.first >= 0) {
            const int normalGap = qRound(6.0 * std::max(1.0, frame.devicePixelRatio()));
            QVERIFY2(sibling.first - child.last - 1 <= normalGap,
                     qPrintable(QStringLiteral("Long branch seam opened "
                                               "at %1 ms: childLast=%2 "
                                               "siblingFirst=%3 offset=%4")
                                    .arg(time)
                                    .arg(child.last)
                                    .arg(sibling.first)
                                    .arg(offset)));
        }
        if (time == 8) {
            QVERIFY(offset > 0);
            QVERIFY(offset < visualTravel / 2);
        }
    }

    tree.finishDisclosureAnimation();
    theme->setAnimationsEnabled(originalAnimations);
}

QTEST_MAIN(DisclosureTreeTest)
#include "tst_disclosuretree.moc"
