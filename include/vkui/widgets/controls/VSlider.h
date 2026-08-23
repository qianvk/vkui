// SPDX-License-Identifier: MIT

#pragma once

#include <QSlider>
#include <memory>
#include <vkui/VkUiGlobal.h>

namespace vkui {

class VSliderPrivate;

/** A QSlider that preserves its exact press-time value during handle dragging. */
class VKUI_WIDGETS_EXPORT VSlider : public QSlider {
    Q_OBJECT

  public:
    explicit VSlider(QWidget* parent = nullptr);
    explicit VSlider(Qt::Orientation orientation, QWidget* parent = nullptr);
    ~VSlider() override;

  protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

  private:
    std::unique_ptr<VSliderPrivate> d;
};

} // namespace vkui
