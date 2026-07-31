// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QMetaObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QVector>
#include <memory>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkFileIcon.h>

class QTimeLine;
class QWheelEvent;
class QPixmap;

namespace vkui {

struct VKUI_WIDGETS_EXPORT VkTreeItemGeometry final {
    QRect iconRect;
    QRect availableTextRect;
    QRect textRect;
    QRect pillRect;
};

/**
 * Returns the canonical compact filesystem-style icon, text, and pill
 * geometry. Delegates sharing this helper always paint identical hover and
 * selection bounds, including hierarchy offsets supplied by QTreeView.
 */
[[nodiscard]] VKUI_WIDGETS_EXPORT VkTreeItemGeometry
treeItemGeometry(const QStyleOptionViewItem& option, const VkFileIconMetrics& metrics,
                 int horizontalInset = 3, int pillHorizontalPadding = 4);

/**
 * A QTreeView with Finder-style, reversible disclosure motion.
 *
 * Expanded descendants retain their complete branch coordinates while only
 * rows intersecting the viewport are painted each frame. One eased reveal
 * boundary moves the true trailing descendant and every following row
 * together, so their seam cannot drift even for branches much taller than the
 * viewport. This avoids allocating a branch-height pixmap. An opposite click
 * reverses the same geometry path without rebuilding the motion segment.
 * Internal current-index auto-scrolling is held until the transaction ends;
 * explicit wheel and scrollbar gestures still take precedence immediately.
 */
class VKUI_WIDGETS_EXPORT VkDisclosureTreeView : public QTreeView {
    Q_OBJECT

  public:
    explicit VkDisclosureTreeView(QWidget* parent = nullptr);
    ~VkDisclosureTreeView() override;

    void setModel(QAbstractItemModel* model) override;

    void setNativeBranchesVisible(bool visible);
    [[nodiscard]] bool nativeBranchesVisible() const noexcept;

    void setExpandedAnimated(const QModelIndex& index, bool expanded);
    void finishDisclosureAnimation();
    void finishRowMutationAnimation();

    /**
     * Sets the solid surface behind captured rows.
     *
     * Transparent views embedded in a painted parent, such as a popover,
     * should pass that parent's semantic background color explicitly.
     */
    void setDisclosureSurfaceColor(const QColor& color);
    [[nodiscard]] QColor disclosureSurfaceColor() const;

  protected:
    void changeEvent(QEvent* event) override;
    void drawBranches(QPainter* painter, const QRect& rect,
                      const QModelIndex& index) const override;
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void updateGeometries() override;
    void wheelEvent(QWheelEvent* event) override;

  private:
    enum class RowMutationKind { Insert, Remove, Move };
    struct RowMutationTransaction;

    [[nodiscard]] bool isDescendantOf(const QModelIndex& index, const QModelIndex& ancestor) const;
    void beginRowMutation(RowMutationKind kind, const QModelIndex& sourceParent, int first,
                          int last, const QModelIndex& destinationParent = {},
                          int destinationRow = -1);
    [[nodiscard]] QPixmap captureMutationRow(const QModelIndex& index, const QRect& rect) const;
    void completeRowMutation(RowMutationKind kind);
    void reconnectMutationModel(QAbstractItemModel* model);
    void updateDisclosureScrollRange(qreal progress);
    void updateMutationScrollRange(qreal progress);

    QTimeLine* m_disclosureTimeline = nullptr;
    QTimeLine* m_mutationTimeline = nullptr;
    QPointer<QWidget> m_disclosureOverlay;
    QPointer<QWidget> m_mutationOverlay;
    std::unique_ptr<RowMutationTransaction> m_pendingMutation;
    QVector<QMetaObject::Connection> m_modelConnections;
    QPersistentModelIndex m_disclosureIndex;
    QColor m_disclosureSurfaceColor;
    int m_disclosureHorizontalScrollValue = 0;
    int m_disclosureVerticalScrollValue = 0;
    int m_disclosureCollapsedScrollMaximum = 0;
    int m_disclosureExpandedScrollMaximum = 0;
    int m_mutationStartScrollMaximum = 0;
    int m_mutationTargetScrollMaximum = 0;
    int m_mutationHorizontalScrollValue = 0;
    int m_mutationVerticalScrollValue = 0;
    bool m_restoringDisclosureScroll = false;
    bool m_restoringMutationScroll = false;
    bool m_nativeBranchesVisible = true;
};

} // namespace vkui
