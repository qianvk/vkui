// SPDX-License-Identifier: MIT

#pragma once

namespace vkui {

class VStyle;
class VkPopupSurfaceStyler;
class VkWidgetTypographyController;

class VStylePrivate final {
  public:
    explicit VStylePrivate(VStyle* owner);
    ~VStylePrivate();

    VStyle* q = nullptr;
    VkPopupSurfaceStyler* popupSurfaces = nullptr;
    VkWidgetTypographyController* typography = nullptr;
};

} // namespace vkui
