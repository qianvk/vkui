// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QVector>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkFileIcon.h>
#include <vkui/widgets/views/VkDisclosureTreeView.h>

namespace vkui {

/** Model roles understood by the default file-tree delegate. */
enum VkFileTreeItemRole {
    VkFileTreePathRole = Qt::UserRole + 0x560,
    VkFileTreeDirectoryRole,
    VkFileTreeBookRole,
    VkFileTreeTagColorsRole,
    VkFileTreeDropTargetRole,
    VkFileTreeGlyphRole,
};

/** Shared presentation contract for one compact file-tree row. */
struct VKUI_WIDGETS_EXPORT VkFileTreeRowPresentation final {
    VkFileGlyph glyph = VkFileGlyph::File;
    QVector<QColor> tagColors;
    bool directory = false;
    bool dropTarget = false;
};

/**
 * Compact Nerd Font file-row renderer shared by filesystem surfaces.
 *
 * Domain delegates may keep specialized cards or source-list rows and call
 * paintFileTreeRow() for their ordinary hierarchy rows. This keeps path and
 * metadata policy outside VkUI while centralizing geometry and visuals.
 */
class VKUI_WIDGETS_EXPORT VkFileTreeDelegate : public QStyledItemDelegate {
    Q_OBJECT

  public:
    static constexpr int HorizontalInset = 3;
    static constexpr int PillHorizontalPadding = 4;
    static constexpr int MinimumRowHeight = 27;

    explicit VkFileTreeDelegate(QTreeView* view, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;

  protected:
    [[nodiscard]] virtual VkFileTreeRowPresentation
    fileTreePresentation(const QModelIndex& index) const;
    void paintFileTreeRow(QPainter* painter, const QStyleOptionViewItem& option,
                          const VkFileTreeRowPresentation& presentation) const;
    [[nodiscard]] int fileTreeRowHeight(const QStyleOptionViewItem& option) const;
    [[nodiscard]] QRect fileTreeEditorRect(const QStyleOptionViewItem& option) const;
    [[nodiscard]] QTreeView* fileTreeView() const noexcept;

  private:
    QPointer<QTreeView> m_view;
};

/**
 * Reusable filesystem tree frontend.
 *
 * VkDisclosureTreeView remains the single animation authority. This class
 * adds stable file-row metrics, compact default view configuration, and the
 * shared Nerd Font delegate; it deliberately owns no filesystem I/O or model.
 */
class VKUI_WIDGETS_EXPORT VkFileTreeView : public VkDisclosureTreeView {
    Q_OBJECT

  public:
    explicit VkFileTreeView(QWidget* parent = nullptr);
    ~VkFileTreeView() override;

    [[nodiscard]] const VkFileIconMetrics& fileTreeMetrics() const noexcept;
    [[nodiscard]] VkTreeItemGeometry
    fileTreeItemGeometry(const QStyleOptionViewItem& option,
                         int horizontalInset = VkFileTreeDelegate::HorizontalInset,
                         int pillHorizontalPadding =
                             VkFileTreeDelegate::PillHorizontalPadding) const;
    [[nodiscard]] QRect fileTreeIconRect(const QModelIndex& index) const;

  protected:
    void changeEvent(QEvent* event) override;
    void setFileTreeMetrics(VkFileIconMetrics metrics, int indentation = -1);
    void refreshFileTreeMetrics();

  private:
    VkFileIconMetrics m_fileTreeMetrics;
};

} // namespace vkui
