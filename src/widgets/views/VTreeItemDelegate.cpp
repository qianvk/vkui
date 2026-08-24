// SPDX-License-Identifier: MIT

#include <QAbstractItemModel>
#include <QApplication>
#include <QBrush>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QVariant>
#include <algorithm>
#include <vkui/core/VkIcon.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/views/VTreeItemDelegate.h>
#include <vkui/widgets/views/VTreeView.h>

namespace vkui {
namespace {

[[nodiscard]] QRect logicalLeadingRect(const QRect& contentRect, const int slotWidth,
                                       const Qt::LayoutDirection direction) {
    if (direction == Qt::RightToLeft) {
        return {contentRect.right() - slotWidth + 1, contentRect.top(), slotWidth,
                contentRect.height()};
    }
    return {contentRect.left(), contentRect.top(), slotWidth, contentRect.height()};
}

[[nodiscard]] QIcon::Mode iconMode(const QStyleOptionViewItem& option) {
    if (!option.state.testFlag(QStyle::State_Enabled)) {
        return QIcon::Disabled;
    }
    if (option.state.testFlag(QStyle::State_Selected)) {
        return QIcon::Selected;
    }
    return option.state.testFlag(QStyle::State_MouseOver) ? QIcon::Active : QIcon::Normal;
}

[[nodiscard]] QSize resolvedIconSize(const VTreeView* view) {
    if (view != nullptr && view->iconSize().isValid()) {
        return view->iconSize();
    }
    const int extent =
        qRound(VkThemeManager::instance()->theme().metrics().fixedControlExtentRegular);
    return {extent, extent};
}

void resolveDefaultItemFont(QStyleOptionViewItem& option, const QModelIndex& index,
                            const VTreeView* view) {
    if (view == nullptr || index.data(Qt::FontRole).isValid()) {
        return;
    }
    option.font = view->font();
    option.fontMetrics = QFontMetrics(option.font);
}

} // namespace

VTreeItemDelegate::VTreeItemDelegate(VTreeView* view, QObject* parent)
    : QStyledItemDelegate(parent), view_(view) {
    Q_ASSERT(view_ != nullptr);
}

VTreeItemDelegate::~VTreeItemDelegate() = default;

VTreeItemPresentation VTreeItemDelegate::treeItemPresentation(const QStyleOptionViewItem& option,
                                                              const QModelIndex& index) const {
    VTreeItemPresentation presentation;
    presentation.text = option.text;
    presentation.trailingText = index.data(VTreeTrailingTextRole).toString();
    presentation.leadingIcon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    presentation.expandable = index.model() != nullptr && index.model()->hasChildren(index);
    presentation.expanded = view_ != nullptr && view_->isExpanded(index);
    const QVariant checkState = index.data(Qt::CheckStateRole);
    presentation.checkable = checkState.isValid();
    if (presentation.checkable) {
        presentation.checkState = static_cast<Qt::CheckState>(checkState.toInt());
    }

    const QVariant foreground = index.data(Qt::ForegroundRole);
    if (foreground.canConvert<QBrush>()) {
        presentation.foreground = qvariant_cast<QBrush>(foreground).color();
    }
    if (presentation.leadingIcon.isNull() && presentation.expandable) {
        const bool pointsLeft = option.direction == Qt::RightToLeft;
        presentation.leadingIcon = icon(
            presentation.expanded ? VkSymbol::ChevronDown
                                  : (pointsLeft ? VkSymbol::ChevronLeft : VkSymbol::ChevronRight),
            VkIconRole::Secondary);
    }
    return presentation;
}

VTreeItemLayout VTreeItemDelegate::layoutTreeItem(const QStyleOptionViewItem& option,
                                                  const QModelIndex& index,
                                                  const VTreeItemPresentation& presentation) const {
    Q_UNUSED(index)
    const QRect backgroundRect = option.rect.adjusted(HorizontalInset, 2, -HorizontalInset, -2);
    const QRect contentRect =
        backgroundRect.adjusted(ContentHorizontalPadding, 0, -ContentHorizontalPadding, 0);
    const int leadingSlotWidth = resolvedIconSize(view_).width();
    const QRect leadingRect = logicalLeadingRect(contentRect, leadingSlotWidth, option.direction);
    const QFontMetrics metrics(option.font);
    const int trailingWidth = presentation.trailingText.isEmpty()
                                  ? 0
                                  : std::min(contentRect.width() / 2,
                                             metrics.horizontalAdvance(presentation.trailingText));

    QRect trailingRect;
    QRect checkRect;
    QRect textRect;
    if (option.direction == Qt::RightToLeft) {
        trailingRect = {contentRect.left(), contentRect.top(), trailingWidth, contentRect.height()};
        const int textLeft =
            trailingRect.isEmpty() ? contentRect.left() : trailingRect.right() + ContentGap + 1;
        int textRight = leadingRect.left() - ContentGap - 1;
        if (presentation.checkable) {
            checkRect = {textRight - leadingSlotWidth + 1, contentRect.top(), leadingSlotWidth,
                         contentRect.height()};
            textRight = checkRect.left() - ContentGap - 1;
        }
        textRect = {textLeft, contentRect.top(), std::max(0, textRight - textLeft + 1),
                    contentRect.height()};
    } else {
        const int trailingLeft = contentRect.right() - trailingWidth + 1;
        trailingRect = {trailingLeft, contentRect.top(), trailingWidth, contentRect.height()};
        int textLeft = leadingRect.right() + ContentGap + 1;
        if (presentation.checkable) {
            checkRect = {textLeft, contentRect.top(), leadingSlotWidth, contentRect.height()};
            textLeft = checkRect.right() + ContentGap + 1;
        }
        const int textRight =
            trailingRect.isEmpty() ? contentRect.right() : trailingRect.left() - ContentGap - 1;
        textRect = {textLeft, contentRect.top(), std::max(0, textRight - textLeft + 1),
                    contentRect.height()};
    }
    return {backgroundRect, contentRect, leadingRect, checkRect, textRect, trailingRect};
}

VTreeItemLayout VTreeItemDelegate::itemLayout(const QStyleOptionViewItem& option,
                                              const QModelIndex& index) const {
    QStyleOptionViewItem resolved(option);
    initStyleOption(&resolved, index);
    resolveDefaultItemFont(resolved, index, view_);
    resolved.state &= ~QStyle::State_HasFocus;
    return layoutTreeItem(resolved, index, treeItemPresentation(resolved, index));
}

void VTreeItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    if (painter == nullptr || !index.isValid()) {
        return;
    }
    QStyleOptionViewItem resolved(option);
    initStyleOption(&resolved, index);
    resolveDefaultItemFont(resolved, index, view_);
    resolved.state &= ~QStyle::State_HasFocus;
    const VTreeItemPresentation presentation = treeItemPresentation(resolved, index);
    const VTreeItemLayout layout = layoutTreeItem(resolved, index, presentation);
    paintTreeItem(painter, resolved, index, layout, presentation);
}

void VTreeItemDelegate::paintTreeItemBackground(QPainter* painter,
                                                const QStyleOptionViewItem& option,
                                                const VTreeItemLayout& layout) const {
    if (painter == nullptr || !layout.backgroundRect.isValid()) {
        return;
    }
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
    const bool hasModelBackground = option.backgroundBrush.style() != Qt::NoBrush;
    if (!hasModelBackground && !selected && !hovered) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    const qreal radius = VkThemeManager::instance()->theme().metrics().cornerRadiusSmall;
    path.addRoundedRect(QRectF(layout.backgroundRect), radius, radius);
    if (hasModelBackground) {
        painter->fillPath(path, option.backgroundBrush);
    }
    if (selected || hovered) {
        QColor interactionFill;
        if (selected) {
            interactionFill = VkThemeManager::instance()->theme().colors().accent;
            interactionFill.setAlphaF(
                option.palette.color(QPalette::Window).lightnessF() < 0.5F ? 0.34F : 0.18F);
        } else {
            interactionFill = option.palette.color(QPalette::Text);
            interactionFill.setAlpha(12);
        }
        painter->fillPath(path, interactionFill);
    }
    painter->restore();
}

void VTreeItemDelegate::paintTreeItem(QPainter* painter, const QStyleOptionViewItem& option,
                                      const QModelIndex& index, const VTreeItemLayout& layout,
                                      const VTreeItemPresentation& presentation) const {
    Q_UNUSED(index)
    paintTreeItemBackground(painter, option, layout);

    QPalette::ColorGroup group = QPalette::Inactive;
    if (!option.state.testFlag(QStyle::State_Enabled)) {
        group = QPalette::Disabled;
    } else if (option.state.testFlag(QStyle::State_Active)) {
        group = QPalette::Active;
    }

    painter->save();
    if (!presentation.leadingIcon.isNull()) {
        const QSize iconSize = resolvedIconSize(view_);
        const QSize bounded = iconSize.boundedTo(layout.leadingRect.size());
        const QRect iconRect(QPoint(layout.leadingRect.center().x() - bounded.width() / 2,
                                    layout.leadingRect.center().y() - bounded.height() / 2),
                             bounded);
        presentation.leadingIcon.paint(painter, iconRect, Qt::AlignCenter, iconMode(option),
                                       presentation.expanded ? QIcon::On : QIcon::Off);
    } else if (!presentation.leadingText.isEmpty()) {
        painter->setPen(option.palette.color(group, QPalette::PlaceholderText));
        painter->drawText(layout.leadingRect, Qt::AlignCenter, presentation.leadingText);
    }

    if (presentation.checkable && layout.checkRect.isValid()) {
        QStyleOptionViewItem checkOption(option);
        checkOption.rect = layout.checkRect;
        checkOption.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange |
                               QStyle::State_HasFocus);
        if (presentation.checkState == Qt::Checked) {
            checkOption.state |= QStyle::State_On;
        } else if (presentation.checkState == Qt::PartiallyChecked) {
            checkOption.state |= QStyle::State_NoChange;
        } else {
            checkOption.state |= QStyle::State_Off;
        }
        QStyle* style = option.widget == nullptr ? QApplication::style() : option.widget->style();
        style->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &checkOption, painter,
                             option.widget);
    }

    const QColor textColor = presentation.foreground.isValid()
                                 ? presentation.foreground
                                 : option.palette.color(group, QPalette::Text);
    painter->setFont(option.font);
    painter->setPen(textColor);
    const QFontMetrics metrics(option.font);
    int textAlignment = static_cast<int>(option.displayAlignment);
    if ((textAlignment & Qt::AlignHorizontal_Mask) == 0) {
        textAlignment |= option.direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft;
    }
    textAlignment = (textAlignment & ~Qt::AlignVertical_Mask) | Qt::AlignVCenter;
    painter->drawText(layout.textRect, textAlignment,
                      metrics.elidedText(presentation.text, option.textElideMode,
                                         std::max(0, layout.textRect.width())));
    if (!presentation.trailingText.isEmpty()) {
        painter->setPen(option.palette.color(group, QPalette::PlaceholderText));
        const int trailingAlignment =
            option.direction == Qt::RightToLeft ? Qt::AlignLeft : Qt::AlignRight;
        painter->drawText(layout.trailingRect, Qt::AlignVCenter | trailingAlignment,
                          presentation.trailingText);
    }
    painter->restore();
}

QSize VTreeItemDelegate::treeItemSizeHint(const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const {
    const QSize explicitHint = qvariant_cast<QSize>(index.data(Qt::SizeHintRole));
    const int contentHeight = std::max(
        {option.fontMetrics.height(), resolvedIconSize(view_).height(), explicitHint.height()});
    return {explicitHint.width(), std::max(MinimumRowHeight, contentHeight + 8)};
}

QSize VTreeItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
    QStyleOptionViewItem resolved(option);
    initStyleOption(&resolved, index);
    resolveDefaultItemFont(resolved, index, view_);
    return treeItemSizeHint(resolved, index);
}

bool VTreeItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                    const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event == nullptr || model == nullptr || !index.isValid()) {
        return false;
    }
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease &&
        event->type() != QEvent::MouseButtonDblClick) {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    const QVariant currentValue = index.data(Qt::CheckStateRole);
    if (!currentValue.isValid() || !index.flags().testFlag(Qt::ItemIsUserCheckable) ||
        !index.flags().testFlag(Qt::ItemIsEnabled)) {
        return false;
    }
    const auto* mouseEvent = static_cast<QMouseEvent*>(event);
    if (mouseEvent->button() != Qt::LeftButton ||
        !itemLayout(option, index).checkRect.contains(mouseEvent->position().toPoint())) {
        return false;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        const Qt::CheckState current = static_cast<Qt::CheckState>(currentValue.toInt());
        const Qt::CheckState next = current == Qt::Checked ? Qt::Unchecked : Qt::Checked;
        return model->setData(index, next, Qt::CheckStateRole);
    }
    return true;
}

void VTreeItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                                             const QModelIndex& index) const {
    if (editor != nullptr) {
        editor->setGeometry(itemLayout(option, index).textRect.adjusted(-2, 1, 2, -1));
    }
}

VTreeView* VTreeItemDelegate::treeView() const noexcept {
    return view_.data();
}

} // namespace vkui
