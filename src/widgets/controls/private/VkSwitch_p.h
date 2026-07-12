// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <vkui/widgets/controls/VkSwitch.h>

class QVariantAnimation;

namespace vkui {

class VkSwitchPrivate final {
  public:
    explicit VkSwitchPrivate(VkSwitch* owner);
    ~VkSwitchPrivate();

    void animateThumb(bool checked);
    void animateHover(bool hovered);
    void animatePress(bool pressed);
    void settleAnimations();

    VkSwitch* q = nullptr;
    VkControlSize controlSize = VkControlSize::Regular;
    std::optional<int> customExtent;
    qreal thumbProgress = 0.0;
    qreal hoverProgress = 0.0;
    qreal pressProgress = 0.0;
    bool keyboardFocusVisible = false;

  private:
    void animateValue(QVariantAnimation*& animation, qreal& value, qreal target,
                      qreal durationScale);

    QVariantAnimation* m_thumbAnimation = nullptr;
    QVariantAnimation* m_hoverAnimation = nullptr;
    QVariantAnimation* m_pressAnimation = nullptr;
};

} // namespace vkui
