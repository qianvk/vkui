// SPDX-License-Identifier: MIT

#include "GalleryFonts.h"

#include <vkui/core/VkFileIcon.h>

namespace gallery {

QFont codeFont(const qreal pointSize) {
    QFont font = vkui::fileIconFont(16);
    if (pointSize > 0.0) {
        font.setPointSizeF(pointSize);
    }
    return font;
}

} // namespace gallery
