// SPDX-License-Identifier: MIT

#include <utility>
#include <vkui/core/VkTheme.h>

namespace vkui {

VkTheme::VkTheme(VkColorTokens colors, VkMetricTokens metrics, VkTypographyTokens typography,
                 VkMotionTokens motion, const int textSizeLevel, const qreal textScale,
                 const VkAppearance effectiveAppearance, const quint64 generation,
                 const quint64 colorGeneration)
    : colors_(std::move(colors)), metrics_(std::move(metrics)), typography_(std::move(typography)),
      motion_(std::move(motion)), textSizeLevel_(textSizeLevel), textScale_(textScale),
      effectiveAppearance_(effectiveAppearance), generation_(generation),
      colorGeneration_(colorGeneration) {}

VkTheme::VkTheme(const VkTheme& other) = default;
VkTheme::VkTheme(VkTheme&& other) noexcept = default;
VkTheme& VkTheme::operator=(const VkTheme& other) = default;
VkTheme& VkTheme::operator=(VkTheme&& other) noexcept = default;
VkTheme::~VkTheme() = default;

const VkColorTokens& VkTheme::colors() const noexcept {
    return colors_;
}

const VkMetricTokens& VkTheme::metrics() const noexcept {
    return metrics_;
}

const VkTypographyTokens& VkTheme::typography() const noexcept {
    return typography_;
}

const VkMotionTokens& VkTheme::motion() const noexcept {
    return motion_;
}

int VkTheme::textSizeLevel() const noexcept {
    return textSizeLevel_;
}

qreal VkTheme::textScale() const noexcept {
    return textScale_;
}

VkAppearance VkTheme::effectiveAppearance() const noexcept {
    return effectiveAppearance_;
}

quint64 VkTheme::generation() const noexcept {
    return generation_;
}

quint64 VkTheme::colorGeneration() const noexcept {
    return colorGeneration_;
}

} // namespace vkui
