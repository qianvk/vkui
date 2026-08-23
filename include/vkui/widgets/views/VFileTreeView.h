// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkFileIcon.h>
#include <vkui/widgets/views/VTreeView.h>

namespace vkui {

class VFileTreeView;

/** Model roles understood by the default file-tree delegate. */
enum VFileTreeItemRole {
    VFileTreePathRole = Qt::UserRole + 0x560,
    VFileTreeDirectoryRole,
    VFileTreeBookRole,
    VFileTreeTagColorsRole,
    VFileTreeDropTargetRole,
    VFileTreeSymbolRole,
};

/** Shared presentation contract for one compact file-tree row. */
struct VKUI_WIDGETS_EXPORT VFileTreeRowPresentation final {
    VkSymbol symbol = VkSymbol::FileGeneric;
    QString leadingText;
    QVector<QColor> tagColors;
    bool directory = false;
    bool dropTarget = false;
};

/**
 * Compact SVG file-row renderer shared by filesystem surfaces.
 *
 * Domain delegates override fileTreePresentation() to map application roles
 * to shared file-row semantics. Geometry, painting, input, and motion remain
 * centralized in VkUI.
 */
class VKUI_WIDGETS_EXPORT VFileTreeDelegate : public VTreeItemDelegate {
    Q_OBJECT

  public:
    static constexpr int MinimumRowHeight = 27;

    explicit VFileTreeDelegate(VFileTreeView* view, QObject* parent = nullptr);

  protected:
    [[nodiscard]] virtual VFileTreeRowPresentation
    fileTreePresentation(const QModelIndex& index) const;
    [[nodiscard]] VTreeItemPresentation
    treeItemPresentation(const QStyleOptionViewItem& option,
                         const QModelIndex& index) const override;
    [[nodiscard]] VTreeItemLayout
    layoutTreeItem(const QStyleOptionViewItem& option, const QModelIndex& index,
                   const VTreeItemPresentation& presentation) const override;
    [[nodiscard]] QSize treeItemSizeHint(const QStyleOptionViewItem& option,
                                         const QModelIndex& index) const override;
    void paintTreeItem(QPainter* painter, const QStyleOptionViewItem& option,
                       const QModelIndex& index, const VTreeItemLayout& layout,
                       const VTreeItemPresentation& presentation) const override;
    [[nodiscard]] VFileTreeView* fileTreeView() const noexcept;
};

/**
 * Reusable filesystem tree frontend.
 *
 * VTreeView remains the single animation authority. This class
 * adds stable file-row metrics, compact default view configuration, and the
 * shared SVG delegate; it deliberately owns no filesystem I/O or model.
 */
class VKUI_WIDGETS_EXPORT VFileTreeView : public VTreeView {
    Q_OBJECT

  public:
    explicit VFileTreeView(QWidget* parent = nullptr);
    ~VFileTreeView() override;

    [[nodiscard]] const VkFileIconMetrics& fileTreeMetrics() const noexcept;
    [[nodiscard]] QRect fileTreeIconRect(const QModelIndex& index) const;

  protected:
    void changeEvent(QEvent* event) override;
    void setFileTreeMetrics(VkFileIconMetrics metrics, int indentation = -1);
    void refreshFileTreeMetrics();

  private:
    VkFileIconMetrics m_fileTreeMetrics;
};

} // namespace vkui
