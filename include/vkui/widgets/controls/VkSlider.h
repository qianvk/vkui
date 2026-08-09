// SPDX-License-Identifier: MIT

#pragma once

#include <QSlider>
#include <memory>
#include <vkui/VkUiGlobal.h>

namespace vkui {

class VkSliderPrivate;

/** A QSlider that preserves its exact press-time value during handle dragging. */
class VKUI_WIDGETS_EXPORT VkSlider : public QSlider {
    Q_OBJECT

  public:
    explicit VkSlider(QWidget* parent = nullptr);
    explicit VkSlider(Qt::Orientation orientation, QWidget* parent = nullptr);
    ~VkSlider() override;

  protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

  private:
    std::unique_ptr<VkSliderPrivate> d;
};

} // namespace vkui
