// SPDX-License-Identifier: MIT

#include <utility>
#include <vkui/core/VkTheme.h>

namespace vkui {

VkTheme::VkTheme(VkColorTokens colors, VkMetricTokens metrics, VkTypographyTokens typography,
                 VkMotionTokens motion, const VkAppearance effectiveAppearance,
                 const quint64 generation)
    : colors_(std::move(colors)), metrics_(std::move(metrics)), typography_(std::move(typography)),
      motion_(std::move(motion)), effectiveAppearance_(effectiveAppearance),
      generation_(generation) {}

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

VkAppearance VkTheme::effectiveAppearance() const noexcept {
    return effectiveAppearance_;
}

quint64 VkTheme::generation() const noexcept {
    return generation_;
}

} // namespace vkui
