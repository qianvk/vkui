// SPDX-License-Identifier: MIT

#include "IconCatalogProxyModel.h"

#include "IconCatalogModel.h"

IconCatalogProxyModel::IconCatalogProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

void IconCatalogProxyModel::setSearchText(QString text) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    searchTerms_ = text.simplified().split(u' ', Qt::SkipEmptyParts);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateRowsFilter();
#endif
}

void IconCatalogProxyModel::setSelectedOnly(const bool selectedOnly) {
    if (selectedOnly_ == selectedOnly) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    selectedOnly_ = selectedOnly;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateRowsFilter();
#endif
}

bool IconCatalogProxyModel::filterAcceptsRow(const int sourceRow,
                                             const QModelIndex& sourceParent) const {
    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    if (selectedOnly_ && !sourceIndex.data(IconCatalogModel::SelectedRole).toBool()) {
        return false;
    }

    const QString searchable = sourceIndex.data(IconCatalogModel::SearchRole).toString();
    for (const QString& term : searchTerms_) {
        if (!searchable.contains(term, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}
