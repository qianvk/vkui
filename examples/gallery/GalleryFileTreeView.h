// SPDX-License-Identifier: MIT

#pragma once

#include <QTreeView>

class GalleryFileTreeView final : public QTreeView {
  public:
    explicit GalleryFileTreeView(QWidget* parent = nullptr);

  protected:
    void drawBranches(QPainter* painter, const QRect& rect,
                      const QModelIndex& index) const override;
};
