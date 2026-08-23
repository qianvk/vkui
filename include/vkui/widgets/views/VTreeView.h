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
#include <vkui/widgets/views/VTreeItemDelegate.h>

class QMouseEvent;
class QTimeLine;
class QWheelEvent;
class QPixmap;

namespace vkui {

/**
 * VkUI's styled tree view with icon-only disclosure interaction.
 *
 * The model owns hierarchy and semantic data, VTreeItemDelegate owns row
 * layout and painting, and this view owns input, expansion state, branch
 * connectors, and all structural animation. Applications can therefore
 * customize item layouts without inheriting or reimplementing animation.
 */
class VKUI_WIDGETS_EXPORT VTreeView : public QTreeView {
    Q_OBJECT

  public:
    explicit VTreeView(QWidget* parent = nullptr);
    ~VTreeView() override;

    void setModel(QAbstractItemModel* model) override;

    void setTreeItemDelegate(VTreeItemDelegate* delegate);
    [[nodiscard]] VTreeItemDelegate* treeItemDelegate() const noexcept;
    [[nodiscard]] VTreeItemLayout itemLayout(const QModelIndex& index) const;
    [[nodiscard]] QRect expansionToggleRect(const QModelIndex& index) const;

    void setBranchLinesVisible(bool visible);
    [[nodiscard]] bool branchLinesVisible() const noexcept;

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

  signals:
    /** Emitted after a valid expandable item's leading icon is activated. */
    void expansionIconClicked(const QModelIndex& index);

  protected:
    void changeEvent(QEvent* event) override;
    void drawBranches(QPainter* painter, const QRect& rect,
                      const QModelIndex& index) const override;
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
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
    [[nodiscard]] QRect mutationRowRect(const QModelIndex& index) const;
    [[nodiscard]] QPixmap captureMutationSurface() const;
    [[nodiscard]] QPixmap captureMutationRow(const QRect& rect, const QPixmap& surface) const;
    void completeRowMutation(RowMutationKind kind);
    void reconnectMutationModel(QAbstractItemModel* model);
    void armExpansionIconPress(const QPoint& position, Qt::MouseButton button);
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
    bool m_branchLinesVisible = false;
    bool m_expansionIconPressed = false;
    QPersistentModelIndex m_pressedExpansionIndex;
};

} // namespace vkui
