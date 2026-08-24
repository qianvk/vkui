// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QtTypes>

namespace vkui {

inline constexpr int VkMinimumTextSizeLevel = 1;
inline constexpr int VkDefaultTextSizeLevel = 3;
inline constexpr int VkMaximumTextSizeLevel = 12;

/** Returns a valid discrete text-size level. */
[[nodiscard]] constexpr int boundedTextSizeLevel(const int level) noexcept {
    return level < VkMinimumTextSizeLevel
               ? VkMinimumTextSizeLevel
               : (level > VkMaximumTextSizeLevel ? VkMaximumTextSizeLevel : level);
}

/**
 * Resolves a text-size level around the macOS 13-point body reference.
 *
 * Each step represents approximately one platform body-font point. The result remains relative so
 * Qt can preserve the actual system font and its platform-specific point/pixel conversion.
 */
[[nodiscard]] constexpr qreal textScaleForTextSizeLevel(const int level) noexcept {
    constexpr qreal referenceBodyPoints = 13.0;
    return (referenceBodyPoints + boundedTextSizeLevel(level) - VkDefaultTextSizeLevel) /
           referenceBodyPoints;
}

} // namespace vkui
