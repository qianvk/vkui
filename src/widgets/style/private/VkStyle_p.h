// SPDX-License-Identifier: MIT

#pragma once

class QWidget;

namespace vkui {

class VkStyle;
class VkPopupSurfaceStyler;
class VkWidgetAnimationManager;

class VkStylePrivate final {
  public:
    explicit VkStylePrivate(VkStyle* owner);
    ~VkStylePrivate();

    static bool supportsAnimations(const QWidget* widget);

    VkStyle* q = nullptr;
    VkWidgetAnimationManager* animations = nullptr;
    VkPopupSurfaceStyler* popupSurfaces = nullptr;
};

} // namespace vkui
