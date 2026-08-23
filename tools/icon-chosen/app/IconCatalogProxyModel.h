// SPDX-License-Identifier: MIT

#pragma once

#include <QSortFilterProxyModel>

class IconCatalogProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT

  public:
    explicit IconCatalogProxyModel(QObject* parent = nullptr);

    void setSearchText(QString text);
    void setSelectedOnly(bool selectedOnly);

  protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const override;

  private:
    QStringList searchTerms_;
    bool selectedOnly_ = false;
};
