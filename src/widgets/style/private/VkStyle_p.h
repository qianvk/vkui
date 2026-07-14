// SPDX-License-Identifier: MIT

#pragma once

namespace vkui {

class VkStyle;
class VkPopupSurfaceStyler;
class VkWidgetAnimationManager;

class VkStylePrivate final {
  public:
    explicit VkStylePrivate(VkStyle* owner);
    ~VkStylePrivate();

    VkStyle* q = nullptr;
    VkWidgetAnimationManager* animations = nullptr;
    VkPopupSurfaceStyler* popupSurfaces = nullptr;
};

} // namespace vkui
