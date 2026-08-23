// SPDX-License-Identifier: MIT

#pragma once

#include <QWidget>

class IconCatalogModel;
class IconCatalogProxyModel;
class QLabel;
class QListView;
class QTimer;

class IconChosenWindow final : public QWidget {
    Q_OBJECT

  public:
    explicit IconChosenWindow(QWidget* parent = nullptr);
    ~IconChosenWindow() override;

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void updateStatus();
    void persistSelection();
    void setFilteredSelection(bool selected);
    void loadSelection();
    void exportSelection();

    IconCatalogModel* model_ = nullptr;
    IconCatalogProxyModel* proxy_ = nullptr;
    QListView* view_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* persistTimer_ = nullptr;
    QString persistencePath_;
};
