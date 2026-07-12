// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMetaType>

namespace vkui {

/** Selects the semantic accent family used when resolving a theme. */
enum class VkAccentColor {
    Blue,
    Purple,
    Pink,
    Red,
    Orange,
    Yellow,
    Green,
    Graphite,
};

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VkAccentColor)
