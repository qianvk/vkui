// SPDX-License-Identifier: MIT

#include "IconCatalogDelegate.h"

#include "IconCatalogModel.h"

#include <QPainter>
#include <QPainterPath>
#include <vkui/core/VkIcon.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>

IconCatalogDelegate::IconCatalogDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize IconCatalogDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {142, 104};
}

void IconCatalogDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    if (painter == nullptr) {
        return;
    }
    const bool selected = index.data(IconCatalogModel::SelectedRole).toBool();
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
    const QColor accent = vkui::VkThemeManager::instance()->theme().colors().accent;
    const QRectF card = QRectF(option.rect).adjusted(3.5, 3.5, -3.5, -3.5);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPainterPath cardPath;
    cardPath.addRoundedRect(card, 10.0, 10.0);
    if (selected || hovered) {
        QColor fill = selected ? accent : option.palette.color(QPalette::Text);
        fill.setAlpha(selected ? 32 : 12);
        painter->fillPath(cardPath, fill);
    }
    if (selected) {
        QColor outline = accent;
        outline.setAlpha(170);
        painter->setPen(QPen(outline, 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(cardPath);
    }

    const QRect iconRect(option.rect.center().x() - 18, option.rect.top() + 14, 36, 36);
    index.data(Qt::DecorationRole)
        .value<QIcon>()
        .paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal,
               selected ? QIcon::On : QIcon::Off);

    if (selected) {
        const QRect checkRect(option.rect.right() - 25, option.rect.top() + 10, 16, 16);
        painter->setPen(Qt::NoPen);
        painter->setBrush(accent);
        painter->drawEllipse(checkRect);
        vkui::icon(vkui::VkSymbol::Checkmark, QColor(Qt::white))
            .paint(painter, checkRect.adjusted(3, 3, -3, -3));
    }

    const QRect nameRect(option.rect.left() + 8, option.rect.top() + 58, option.rect.width() - 16,
                         19);
    const QString name =
        option.fontMetrics.elidedText(index.data().toString(), Qt::ElideMiddle, nameRect.width());
    painter->setPen(option.palette.color(QPalette::Text));
    painter->setFont(option.font);
    painter->drawText(nameRect, Qt::AlignCenter, name);

    QFont detailFont = option.font;
    detailFont.setPointSizeF(std::max<qreal>(8.0, detailFont.pointSizeF() - 1.0));
    painter->setFont(detailFont);
    painter->setPen(option.palette.color(QPalette::PlaceholderText));
    painter->drawText(
        QRect(option.rect.left() + 8, option.rect.top() + 77, option.rect.width() - 16, 17),
        Qt::AlignCenter,
        QStringLiteral("U+%1").arg(index.data(IconCatalogModel::CodePointRole).toString()));
    painter->restore();
}
