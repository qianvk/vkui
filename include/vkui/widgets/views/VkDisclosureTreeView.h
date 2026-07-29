// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkFileIcon.h>

class QTimeLine;
class QWheelEvent;

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
 * Expanded descendants and collapsed following rows are captured in two
 * viewport-bounded surfaces. One eased reveal boundary exposes descendants
 * above it and moves every following row below it, keeping the two regions
 * synchronized without allocating a pixmap as tall as a large model branch.
 * The moving surface ends on a complete item boundary, so descendants and
 * following rows retain their normal seam throughout the motion. An opposite
 * click reverses the same geometry path without rebuilding the motion segment.
 * Internal current-index auto-scrolling is held until the transaction ends;
 * explicit wheel and scrollbar gestures still take precedence immediately.
 */
class VKUI_WIDGETS_EXPORT VkDisclosureTreeView : public QTreeView {
    Q_OBJECT

  public:
    explicit VkDisclosureTreeView(QWidget* parent = nullptr);
    ~VkDisclosureTreeView() override;

    void setExpandedAnimated(const QModelIndex& index, bool expanded);
    void finishDisclosureAnimation();

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
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent* event) override;

  private:
    [[nodiscard]] bool isDescendantOf(const QModelIndex& index, const QModelIndex& ancestor) const;
    [[nodiscard]] int expandedBranchHeight(const QModelIndex& index) const;

    QTimeLine* m_disclosureTimeline = nullptr;
    QPointer<QWidget> m_disclosureOverlay;
    QPersistentModelIndex m_disclosureIndex;
    QColor m_disclosureSurfaceColor;
    int m_disclosureHorizontalScrollValue = 0;
    int m_disclosureVerticalScrollValue = 0;
    bool m_restoringDisclosureScroll = false;
};

} // namespace vkui
