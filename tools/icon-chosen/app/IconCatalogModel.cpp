// SPDX-License-Identifier: MIT

#include "IconCatalogModel.h"

#include "NerdSymbolIcon.h"

#include <QFile>
#include <QTextStream>
#include <algorithm>

IconCatalogModel::IconCatalogModel(QObject* parent) : QAbstractListModel(parent) {
    loadCatalog();
}

int IconCatalogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant IconCatalogModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) {
        return {};
    }
    const IconCatalogEntry& item = entries_.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return item.name;
    case Qt::DecorationRole:
        return nerdSymbolIcon(item.pack, item.element);
    case Qt::ToolTipRole:
        return QStringLiteral("%1\nU+%2").arg(item.name, item.codePoint);
    case Qt::CheckStateRole:
        return item.selected ? Qt::Checked : Qt::Unchecked;
    case SearchRole: {
        QString searchable = item.name;
        searchable.replace(u'-', u' ');
        searchable.replace(u'_', u' ');
        return QStringLiteral("%1 %2 U+%2").arg(searchable, item.codePoint);
    }
    case CodePointRole:
        return item.codePoint;
    case SelectedRole:
        return item.selected;
    default:
        return {};
    }
}

Qt::ItemFlags IconCatalogModel::flags(const QModelIndex& index) const {
    return QAbstractListModel::flags(index) | Qt::ItemIsUserCheckable;
}

bool IconCatalogModel::setData(const QModelIndex& index, const QVariant& value, const int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size() ||
        (role != Qt::CheckStateRole && role != SelectedRole)) {
        return false;
    }

    const bool selected =
        role == Qt::CheckStateRole ? value.toInt() == Qt::Checked : value.toBool();
    IconCatalogEntry& item = entries_[index.row()];
    if (item.selected == selected) {
        return false;
    }
    item.selected = selected;
    selectedCount_ += selected ? 1 : -1;
    emit dataChanged(index, index, {Qt::CheckStateRole, SelectedRole});
    emit selectionChanged(selectedCount_);
    return true;
}

int IconCatalogModel::selectedCount() const noexcept {
    return selectedCount_;
}

const IconCatalogEntry& IconCatalogModel::entry(const int row) const {
    return entries_.at(row);
}

QList<IconCatalogEntry> IconCatalogModel::selectedEntries() const {
    QList<IconCatalogEntry> result;
    result.reserve(selectedCount_);
    for (const IconCatalogEntry& item : entries_) {
        if (item.selected) {
            result.append(item);
        }
    }
    return result;
}

int IconCatalogModel::restoreSelection(const QSet<QString>& names) {
    selectedCount_ = 0;
    for (IconCatalogEntry& item : entries_) {
        item.selected = names.contains(item.name);
        selectedCount_ += item.selected ? 1 : 0;
    }
    if (!entries_.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(entries_.size()) - 1),
                         {Qt::CheckStateRole, SelectedRole});
    }
    emit selectionChanged(selectedCount_);
    return selectedCount_;
}

void IconCatalogModel::setRowsSelected(QList<int> rows, const bool selected) {
    if (rows.isEmpty()) {
        return;
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    int firstChanged = static_cast<int>(entries_.size());
    int lastChanged = -1;
    for (const int row : std::as_const(rows)) {
        if (row < 0 || row >= entries_.size() || entries_[row].selected == selected) {
            continue;
        }
        entries_[row].selected = selected;
        selectedCount_ += selected ? 1 : -1;
        firstChanged = std::min(firstChanged, row);
        lastChanged = std::max(lastChanged, row);
    }
    if (lastChanged < firstChanged) {
        return;
    }
    emit dataChanged(index(firstChanged), index(lastChanged), {Qt::CheckStateRole, SelectedRole});
    emit selectionChanged(selectedCount_);
}

void IconCatalogModel::loadCatalog() {
    QFile file(QStringLiteral(":/icon-chosen/catalog/catalog.tsv"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream input(&file);
    while (!input.atEnd()) {
        const QString line = input.readLine();
        if (line.startsWith(u'#') || line.isEmpty()) {
            continue;
        }
        const QStringList fields = line.split(u'\t');
        if (fields.size() != 4) {
            continue;
        }
        entries_.append(IconCatalogEntry{fields.at(0), fields.at(1), fields.at(2), fields.at(3)});
    }
}
