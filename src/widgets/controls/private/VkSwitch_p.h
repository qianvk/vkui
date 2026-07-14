// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <vkui/widgets/controls/VkSwitch.h>

namespace vkui {

class VkWidgetAnimation;

class VkSwitchPrivate final {
  public:
    explicit VkSwitchPrivate(VkSwitch* owner);
    ~VkSwitchPrivate();

    void animateThumb(bool checked);
    void setHovered(bool hovered);
    void setPressed(bool pressed);

    VkSwitch* q = nullptr;
    VkControlSize controlSize = VkControlSize::Regular;
    std::optional<int> customExtent;
    qreal thumbProgress = 0.0;
    qreal hoverProgress = 0.0;
    qreal pressProgress = 0.0;
    bool keyboardFocusVisible = false;

  private:
    void setInteractionValue(qreal& value, bool active);

    VkWidgetAnimation* m_thumbAnimation = nullptr;
};

} // namespace vkui
