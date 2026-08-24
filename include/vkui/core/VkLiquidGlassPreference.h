// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QtTypes>

namespace vkui {

inline constexpr int VkMinimumLiquidGlassTintLevel = 0;
inline constexpr int VkDefaultLiquidGlassTintLevel = 0;
inline constexpr int VkMaximumLiquidGlassTintLevel = 100;

/** Returns a valid Clear-to-Tinted Liquid Glass preference. */
[[nodiscard]] constexpr int boundedLiquidGlassTintLevel(const int level) noexcept {
    return level < VkMinimumLiquidGlassTintLevel
               ? VkMinimumLiquidGlassTintLevel
               : (level > VkMaximumLiquidGlassTintLevel ? VkMaximumLiquidGlassTintLevel : level);
}

/** Converts the public integer preference to a stable zero-to-one renderer input. */
[[nodiscard]] constexpr qreal normalizedLiquidGlassTintLevel(const int level) noexcept {
    return static_cast<qreal>(boundedLiquidGlassTintLevel(level) - VkMinimumLiquidGlassTintLevel) /
           static_cast<qreal>(VkMaximumLiquidGlassTintLevel - VkMinimumLiquidGlassTintLevel);
}

} // namespace vkui
