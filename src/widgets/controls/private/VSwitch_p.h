// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <vkui/widgets/controls/VSwitch.h>

namespace vkui {

class VkWidgetAnimation;

class VSwitchPrivate final {
  public:
    explicit VSwitchPrivate(VSwitch* owner);
    ~VSwitchPrivate();

    void animateThumb(bool checked);
    void setHovered(bool hovered);
    void setPressed(bool pressed);

    VSwitch* q = nullptr;
    VControlSize controlSize = VControlSize::Regular;
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
