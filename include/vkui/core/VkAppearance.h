// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMetaType>

namespace vkui {

/** Selects the requested application appearance. */
enum class VkAppearance {
    Light,
    Dark,
    Auto,
};

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VkAppearance)
