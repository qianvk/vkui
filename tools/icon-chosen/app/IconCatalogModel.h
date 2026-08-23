// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractListModel>
#include <QSet>

struct IconCatalogEntry final {
    QString name;
    QString codePoint;
    QString pack;
    QString element;
    bool selected = false;
};

class IconCatalogModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        SearchRole = Qt::UserRole + 1,
        CodePointRole,
        SelectedRole,
    };

    explicit IconCatalogModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

    [[nodiscard]] int selectedCount() const noexcept;
    [[nodiscard]] const IconCatalogEntry& entry(int row) const;
    [[nodiscard]] QList<IconCatalogEntry> selectedEntries() const;
    [[nodiscard]] int restoreSelection(const QSet<QString>& names);
    void setRowsSelected(QList<int> rows, bool selected);

  signals:
    void selectionChanged(int selectedCount);

  private:
    void loadCatalog();

    QList<IconCatalogEntry> entries_;
    int selectedCount_ = 0;
};
