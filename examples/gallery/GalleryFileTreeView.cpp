// SPDX-License-Identifier: MIT

#include "GalleryFileTreeView.h"

#include <QHeaderView>
#include <QPainter>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>

GalleryFileTreeView::GalleryFileTreeView(QWidget* parent) : QTreeView(parent) {
    setHeaderHidden(true);
    setFrameShape(QFrame::NoFrame);
    setRootIsDecorated(false);
    setIndentation(24);
    setUniformRowHeights(true);
    setAlternatingRowColors(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::NoSelection);
    setExpandsOnDoubleClick(true);
    setAnimated(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, false);
    QPalette transparent = palette();
    transparent.setColor(QPalette::Base, Qt::transparent);
    transparent.setColor(QPalette::AlternateBase, Qt::transparent);
    transparent.setColor(QPalette::Window, Qt::transparent);
    setPalette(transparent);
    viewport()->setPalette(transparent);
    header()->setStretchLastSection(true);
}

void GalleryFileTreeView::drawBranches(QPainter* painter, const QRect& rect,
                                       const QModelIndex& index) const {
    if (!painter || !index.isValid() || !index.parent().isValid()) {
        return;
    }

    const QModelIndex parent = index.parent();
    if (!isExpanded(parent) || model()->rowCount(parent) == 0) {
        return;
    }

    const bool isLastChild = index.row() == model()->rowCount(parent) - 1;
    const int contentLeft = rect.right() + 1;
    const qreal lineX = contentLeft - indentation() * 0.5;
    const qreal centerY = rect.center().y() + 0.5;

    painter->save();
    QPen pen(vkui::VkThemeManager::instance()->theme().colors().separator, 0.0);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);

    // Each row contributes one continuous vertical segment. The last child
    // stops at its center and receives the terminal horizontal connector.
    painter->drawLine(QPointF(lineX, rect.top()),
                      QPointF(lineX, isLastChild ? centerY : rect.bottom() + 1.0));
    painter->drawLine(QPointF(lineX, centerY), QPointF(contentLeft - 4.0, centerY));
    painter->restore();
}
