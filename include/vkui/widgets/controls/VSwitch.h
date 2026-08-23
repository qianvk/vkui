// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractButton>
#include <memory>
#include <vkui/VkUiGlobal.h>
#include <vkui/widgets/VControlSize.h>

namespace vkui {

class VSwitchPrivate;

/** A compact, accessible on/off control backed by QAbstractButton semantics. */
class VKUI_WIDGETS_EXPORT VSwitch final : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(vkui::VControlSize controlSize READ controlSize WRITE setControlSize NOTIFY
                   controlSizeChanged)
    Q_PROPERTY(int controlExtent READ controlExtent WRITE setControlExtent RESET resetControlExtent
                   NOTIFY controlExtentChanged)

  public:
    explicit VSwitch(QWidget* parent = nullptr);
    ~VSwitch() override;

    VControlSize controlSize() const noexcept;
    void setControlSize(VControlSize size);
    int controlExtent() const noexcept;
    void setControlExtent(int logicalPixels);
    void resetControlExtent();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  signals:
    void controlSizeChanged(vkui::VControlSize size);
    void controlExtentChanged(int logicalPixels);

  protected:
    void paintEvent(QPaintEvent* event) override;
    bool hitButton(const QPoint& position) const override;
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;

  private:
    // A switch cannot be made non-checkable through its concrete API.
    using QAbstractButton::setCheckable;

    std::unique_ptr<VSwitchPrivate> d;
};

} // namespace vkui
