// SPDX-License-Identifier: MIT

#pragma once

#include <QPersistentModelIndex>
#include <QPointer>
#include <QColor>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkFileIcon.h>

class QTimeLine;

namespace vkui {

struct VKUI_WIDGETS_EXPORT VkTreeItemGeometry final
{
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
treeItemGeometry(
    const QStyleOptionViewItem &option,
    const VkFileIconMetrics &metrics,
    int horizontalInset = 3,
    int pillHorizontalPadding = 4);

/**
 * A QTreeView with Finder-style, reversible disclosure motion.
 *
 * Children and all following siblings are captured in one expanded surface.
 * The complete surface translates from minus the branch height to zero, so
 * descendants emerge from the folder edge while every relative item position
 * stays invariant and no blank seam can form. The first visible step is
 * derived from the trailing child row's rendered pixels, preventing following
 * siblings from moving before descendant content appears. An opposite click
 * reverses the same geometry path without rebuilding the motion segment.
 */
class VKUI_WIDGETS_EXPORT VkDisclosureTreeView : public QTreeView
{
    Q_OBJECT

public:
    explicit VkDisclosureTreeView(QWidget *parent = nullptr);
    ~VkDisclosureTreeView() override;

    void setExpandedAnimated(
        const QModelIndex &index,
        bool expanded);
    void finishDisclosureAnimation();

    /**
     * Sets the solid surface behind captured rows.
     *
     * Transparent views embedded in a painted parent, such as a popover,
     * should pass that parent's semantic background color explicitly.
     */
    void setDisclosureSurfaceColor(const QColor &color);
    [[nodiscard]] QColor disclosureSurfaceColor() const;

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    [[nodiscard]] bool isDescendantOf(
        const QModelIndex &index,
        const QModelIndex &ancestor) const;
    [[nodiscard]] int expandedBranchHeight(
        const QModelIndex &index) const;

    QTimeLine *m_disclosureTimeline = nullptr;
    QPointer<QWidget> m_disclosureOverlay;
    QPersistentModelIndex m_disclosureIndex;
    QColor m_disclosureSurfaceColor;
};

} // namespace vkui
