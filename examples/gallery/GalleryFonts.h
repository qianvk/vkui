// SPDX-License-Identifier: MIT

#pragma once

#include <QFont>
#include <QIcon>
#include <vkui/core/VkIcon.h>

namespace gallery {

/** Loads the Gallery-only Fira Code Nerd Font resource on first use. */
bool initializeFonts();

/** Returns Fira Code with a platform fixed-font fallback. */
QFont codeFont(qreal pointSize = -1.0);

/** Creates a theme-aware Nerd Font glyph icon. */
QIcon nerdIcon(char32_t codePoint, vkui::VkIconRole role = vkui::VkIconRole::Secondary);

/** Creates a Nerd Font glyph icon with an explicit, non-semantic color. */
QIcon nerdIcon(char32_t codePoint, const QColor& color);

} // namespace gallery
