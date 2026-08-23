// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QIcon>
#include <QPointer>
#include <QString>
#include <QStyledItemDelegate>
#include <QVector>
#include <vkui/VkUiGlobal.h>

namespace vkui {

class VTreeView;

/** Additional semantic roles understood by VTreeItemDelegate. */
enum VTreeItemRole {
    VTreeTrailingTextRole = Qt::UserRole + 0x520,
};

/** Semantic content resolved for one tree item. */
struct VKUI_WIDGETS_EXPORT VTreeItemPresentation {
    QString text;
    QString leadingText;
    QString trailingText;
    QIcon leadingIcon;
    QColor foreground;
    QVector<QColor> trailingIndicators;
    bool expandable = false;
    bool expanded = false;
    bool dropTarget = false;
    bool checkable = false;
    Qt::CheckState checkState = Qt::Unchecked;
};

/**
 * Complete viewport geometry for one tree item.
 *
 * The view uses leadingRect for expansion hit testing, so a custom delegate
 * must return the same layout that it paints. This keeps visual and input
 * geometry authoritative without storing pixel coordinates in the model.
 */
struct VKUI_WIDGETS_EXPORT VTreeItemLayout {
    QRect backgroundRect;
    QRect contentRect;
    QRect leadingRect;
    QRect checkRect;
    QRect textRect;
    QRect trailingRect;
};

/**
 * VkUI's reusable tree-row renderer and layout contract.
 *
 * Applications normally provide standard Qt item roles and override
 * treeItemPresentation() for additional semantic data. A genuinely different
 * row structure can override layoutTreeItem() and paintTreeItem() together. The
 * disclosure animation remains entirely in VTreeView and is automatically
 * applied to every delegate installed on that view.
 */
class VKUI_WIDGETS_EXPORT VTreeItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

  public:
    static constexpr int HorizontalInset = 3;
    static constexpr int ContentHorizontalPadding = 6;
    static constexpr int MinimumRowHeight = 28;
    static constexpr int LeadingSlotSize = 18;
    static constexpr int ContentGap = 6;

    explicit VTreeItemDelegate(VTreeView* view, QObject* parent = nullptr);
    ~VTreeItemDelegate() override;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const final;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const final;
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) final;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const final;

    /** Returns the exact geometry used by paint() for this index. */
    [[nodiscard]] VTreeItemLayout itemLayout(const QStyleOptionViewItem& option,
                                             const QModelIndex& index) const;

  protected:
    [[nodiscard]] virtual VTreeItemPresentation
    treeItemPresentation(const QStyleOptionViewItem& option,
                         const QModelIndex& index) const;
    [[nodiscard]] virtual VTreeItemLayout
    layoutTreeItem(const QStyleOptionViewItem& option, const QModelIndex& index,
                   const VTreeItemPresentation& presentation) const;
    [[nodiscard]] virtual QSize treeItemSizeHint(const QStyleOptionViewItem& option,
                                                 const QModelIndex& index) const;
    virtual void paintTreeItem(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index, const VTreeItemLayout& layout,
                               const VTreeItemPresentation& presentation) const;

    void paintTreeItemBackground(QPainter* painter, const QStyleOptionViewItem& option,
                                 const VTreeItemLayout& layout) const;
    [[nodiscard]] VTreeView* treeView() const noexcept;

  private:
    QPointer<VTreeView> view_;
};

} // namespace vkui
