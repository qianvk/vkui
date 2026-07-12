// SPDX-License-Identifier: MIT

#pragma once

#include <QWidget>
#include <vkui/core/VkMotion.h>

class QLabel;
class QProgressBar;
class QPropertyAnimation;

class MotionPage final : public QWidget {
    Q_OBJECT

  public:
    explicit MotionPage(QWidget* parent = nullptr);

  private:
    void play(vkui::VkMotionRole role, const QString& name);

    QProgressBar* progress_ = nullptr;
    QLabel* specification_ = nullptr;
    QPropertyAnimation* animation_ = nullptr;
};
