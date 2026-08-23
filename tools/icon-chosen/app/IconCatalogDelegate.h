// SPDX-License-Identifier: MIT

#pragma once

#include <QStyledItemDelegate>

class IconCatalogDelegate final : public QStyledItemDelegate {
    Q_OBJECT

  public:
    explicit IconCatalogDelegate(QObject* parent = nullptr);

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};
