// SPDX-License-Identifier: MIT

#pragma once

#include <QWidget>

namespace vkui {
class VkPopover;
}

class QCheckBox;
class QComboBox;
class QPushButton;

class PopoverPage final : public QWidget {
    Q_OBJECT

  public:
    explicit PopoverPage(QWidget* parent = nullptr);

  private:
    QWidget* makePopoverContent();
    void openForAnchor(QWidget* anchor, const QRect& subRect = {});

    vkui::VkPopover* popover_ = nullptr;
    QComboBox* placementBox_ = nullptr;
    QCheckBox* largeContent_ = nullptr;
};
