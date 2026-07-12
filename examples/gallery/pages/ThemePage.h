// SPDX-License-Identifier: MIT

#pragma once

#include <QWidget>

class QLabel;

class ThemePage final : public QWidget {
    Q_OBJECT

  public:
    explicit ThemePage(QWidget* parent = nullptr);

  private:
    void updateSummary();

    QLabel* effectiveLabel_ = nullptr;
    QLabel* accentLabel_ = nullptr;
    QLabel* generationLabel_ = nullptr;
};
