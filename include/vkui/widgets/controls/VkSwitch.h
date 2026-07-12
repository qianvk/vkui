// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractButton>
#include <memory>
#include <vkui/VkUiGlobal.h>
#include <vkui/widgets/VkControlSize.h>

namespace vkui {

class VkSwitchPrivate;

/** A compact, accessible on/off control backed by QAbstractButton semantics. */
class VKUI_WIDGETS_EXPORT VkSwitch final : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(vkui::VkControlSize controlSize READ controlSize WRITE setControlSize NOTIFY
                   controlSizeChanged)
    Q_PROPERTY(int controlExtent READ controlExtent WRITE setControlExtent RESET resetControlExtent
                   NOTIFY controlExtentChanged)

  public:
    explicit VkSwitch(QWidget* parent = nullptr);
    ~VkSwitch() override;

    VkControlSize controlSize() const noexcept;
    void setControlSize(VkControlSize size);
    int controlExtent() const noexcept;
    void setControlExtent(int logicalPixels);
    void resetControlExtent();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  signals:
    void controlSizeChanged(vkui::VkControlSize size);
    void controlExtentChanged(int logicalPixels);

  protected:
    void paintEvent(QPaintEvent* event) override;
    bool hitButton(const QPoint& position) const override;
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;

  private:
    // A switch cannot be made non-checkable through its concrete API.
    using QAbstractButton::setCheckable;

    std::unique_ptr<VkSwitchPrivate> d;
};

} // namespace vkui
