// SPDX-License-Identifier: MIT

#include <QAbstractItemView>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariant>
#include <algorithm>
#include <utility>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/views/VFileTreeView.h>

namespace vkui {
namespace {

constexpr int TagDiameter = 6;
constexpr int TagGap = 3;
constexpr int TagTrailingPadding = 5;

[[nodiscard]] int tagAreaWidth(const qsizetype count) {
    return count <= 0 ? 0
                      : static_cast<int>(count) * TagDiameter +
                            (static_cast<int>(count) - 1) * TagGap + TagTrailingPadding;
}

} // namespace

VFileTreeDelegate::VFileTreeDelegate(VFileTreeView* view, QObject* parent)
    : VTreeItemDelegate(view, parent) {}

VFileTreeRowPresentation VFileTreeDelegate::fileTreePresentation(const QModelIndex& index) const {
    VFileTreeRowPresentation presentation;
    presentation.directory = index.data(VFileTreeDirectoryRole).toBool();
    presentation.dropTarget = index.data(VFileTreeDropTargetRole).toBool();

    const QVariant explicitSymbol = index.data(VFileTreeSymbolRole);
    if (explicitSymbol.isValid()) {
        presentation.symbol = static_cast<VkSymbol>(explicitSymbol.toInt());
    } else if (presentation.directory) {
        presentation.symbol = fileTreeView() != nullptr && fileTreeView()->isExpanded(index)
                                  ? VkSymbol::FileFolderOpen
                                  : VkSymbol::FileFolderClosed;
    } else if (index.data(VFileTreeBookRole).toBool()) {
        presentation.symbol = VkSymbol::FileBook;
    } else {
        presentation.symbol = fileSymbolForPath(index.data(VFileTreePathRole).toString());
    }

    const QVariantList tags = index.data(VFileTreeTagColorsRole).toList();
    presentation.tagColors.reserve(std::min<qsizetype>(3, tags.size()));
    for (const QVariant& tag : tags) {
        const QColor color = tag.value<QColor>();
        if (color.isValid()) {
            presentation.tagColors.append(color);
            if (presentation.tagColors.size() == 3) {
                break;
            }
        }
    }
    return presentation;
}

VTreeItemPresentation VFileTreeDelegate::treeItemPresentation(const QStyleOptionViewItem& option,
                                                              const QModelIndex& index) const {
    VTreeItemPresentation presentation = VTreeItemDelegate::treeItemPresentation(option, index);
    const VFileTreeRowPresentation file = fileTreePresentation(index);
    presentation.leadingText = file.leadingText;
    presentation.leadingIcon =
        file.leadingText.isEmpty()
            ? icon(file.symbol, file.directory ? VkIconRole::Accent : VkIconRole::Secondary)
            : QIcon{};
    presentation.trailingIndicators = file.tagColors;
    presentation.dropTarget = file.dropTarget;
    return presentation;
}

VTreeItemLayout VFileTreeDelegate::layoutTreeItem(const QStyleOptionViewItem& option,
                                                  const QModelIndex& index,
                                                  const VTreeItemPresentation& presentation) const {
    Q_UNUSED(index)
    const VkFileIconMetrics metrics =
        fileTreeView() == nullptr
            ? fileIconMetrics(option.font)
            : (option.font == fileTreeView()->font() ? fileTreeView()->fileTreeMetrics()
                                                     : fileIconMetrics(option.font));
    const QRect backgroundRect = option.rect.adjusted(HorizontalInset, 2, -HorizontalInset, -2);
    const QRect contentRect =
        backgroundRect.adjusted(ContentHorizontalPadding, 0, -ContentHorizontalPadding, 0);
    const int slotWidth = metrics.iconSize.width();
    const bool rightToLeft = option.direction == Qt::RightToLeft;
    const QRect leadingRect =
        rightToLeft ? QRect(contentRect.right() - slotWidth + 1, contentRect.top(), slotWidth,
                            contentRect.height())
                    : QRect(contentRect.left(), contentRect.top(), slotWidth, contentRect.height());
    const int tagsWidth = tagAreaWidth(presentation.trailingIndicators.size());

    QRect trailingRect;
    QRect checkRect;
    QRect textRect;
    if (rightToLeft) {
        trailingRect =
            QRect(contentRect.left(), contentRect.top(), tagsWidth, contentRect.height());
        const int textLeft = trailingRect.isEmpty() ? contentRect.left()
                                                    : trailingRect.right() + metrics.textGap + 1;
        int textRight = leadingRect.left() - metrics.textGap - 1;
        if (presentation.checkable) {
            checkRect = QRect(textRight - metrics.iconSize.width() + 1, contentRect.top(),
                              metrics.iconSize.width(), contentRect.height());
            textRight = checkRect.left() - metrics.textGap - 1;
        }
        textRect = QRect(textLeft, contentRect.top(), std::max(0, textRight - textLeft + 1),
                         contentRect.height());
    } else {
        trailingRect = QRect(contentRect.right() - tagsWidth + 1, contentRect.top(), tagsWidth,
                             contentRect.height());
        int textLeft = leadingRect.right() + metrics.textGap + 1;
        if (presentation.checkable) {
            checkRect =
                QRect(textLeft, contentRect.top(), metrics.iconSize.width(), contentRect.height());
            textLeft = checkRect.right() + metrics.textGap + 1;
        }
        const int textRight = trailingRect.isEmpty() ? contentRect.right()
                                                     : trailingRect.left() - metrics.textGap - 1;
        textRect = QRect(textLeft, contentRect.top(), std::max(0, textRight - textLeft + 1),
                         contentRect.height());
    }
    return {backgroundRect, contentRect, leadingRect, checkRect, textRect, trailingRect};
}

QSize VFileTreeDelegate::treeItemSizeHint(const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const {
    const QSize base = VTreeItemDelegate::treeItemSizeHint(option, index);
    const int metricHeight =
        fileTreeView() == nullptr
            ? fileIconMetrics(option.font).rowHeight
            : (option.font == fileTreeView()->font() ? fileTreeView()->fileTreeMetrics().rowHeight
                                                     : fileIconMetrics(option.font).rowHeight);
    return {base.width(), std::max({MinimumRowHeight, base.height(), metricHeight})};
}

void VFileTreeDelegate::paintTreeItem(QPainter* painter, const QStyleOptionViewItem& option,
                                      const QModelIndex& index, const VTreeItemLayout& layout,
                                      const VTreeItemPresentation& presentation) const {
    QStyleOptionViewItem contentOption(option);
    if (presentation.dropTarget && painter != nullptr) {
        QColor fill = option.palette.color(QPalette::Highlight);
        fill.setAlphaF(option.palette.color(QPalette::Window).lightnessF() < 0.5F ? 0.42F : 0.22F);
        QPainterPath path;
        const qreal radius = VkThemeManager::instance()->theme().metrics().cornerRadiusSmall;
        path.addRoundedRect(QRectF(layout.backgroundRect), radius, radius);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->fillPath(path, fill);
        QColor outline = option.palette.color(QPalette::Highlight);
        outline.setAlpha(220);
        painter->setPen(QPen(outline, 1.25));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
        painter->restore();
        contentOption.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
    }

    VTreeItemDelegate::paintTreeItem(painter, contentOption, index, layout, presentation);
    if (painter == nullptr || presentation.trailingIndicators.isEmpty()) {
        return;
    }

    const int count = static_cast<int>(presentation.trailingIndicators.size());
    const int totalWidth = count * TagDiameter + (count - 1) * TagGap;
    qreal x = option.direction == Qt::RightToLeft ? layout.trailingRect.left()
                                                  : layout.trailingRect.right() - totalWidth + 1;
    const qreal y = layout.trailingRect.center().y() - TagDiameter / 2.0;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    for (const QColor& color : presentation.trailingIndicators) {
        painter->setBrush(color);
        painter->drawEllipse(QRectF(x, y, TagDiameter, TagDiameter));
        x += TagDiameter + TagGap;
    }
    painter->restore();
}

VFileTreeView* VFileTreeDelegate::fileTreeView() const noexcept {
    return qobject_cast<VFileTreeView*>(treeView());
}

VFileTreeView::VFileTreeView(QWidget* parent) : VTreeView(parent) {
    VTreeItemDelegate* defaultDelegate = treeItemDelegate();
    setUniformRowHeights(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setBranchLinesVisible(true);
    setTreeItemDelegate(new VFileTreeDelegate(this, this));
    delete defaultDelegate;
    refreshFileTreeMetrics();
}

VFileTreeView::~VFileTreeView() = default;

const VkFileIconMetrics& VFileTreeView::fileTreeMetrics() const noexcept {
    return m_fileTreeMetrics;
}

QRect VFileTreeView::fileTreeIconRect(const QModelIndex& index) const {
    return itemLayout(index).leadingRect;
}

void VFileTreeView::changeEvent(QEvent* event) {
    VTreeView::changeEvent(event);
    if (event == nullptr) {
        return;
    }
    if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange ||
        event->type() == QEvent::StyleChange || event->type() == QEvent::DevicePixelRatioChange) {
        refreshFileTreeMetrics();
    }
}

void VFileTreeView::setFileTreeMetrics(VkFileIconMetrics metrics, const int indentation) {
    m_fileTreeMetrics = std::move(metrics);
    setIndentation(indentation >= 0
                       ? indentation
                       : m_fileTreeMetrics.iconSize.width() + m_fileTreeMetrics.textGap);
    setIconSize(m_fileTreeMetrics.iconSize);
    scheduleDelayedItemsLayout();
    viewport()->update();
}

void VFileTreeView::refreshFileTreeMetrics() {
    setFileTreeMetrics(fileIconMetrics(font()));
}

} // namespace vkui
