// SPDX-License-Identifier: MIT

#pragma once

#include <vkui/widgets/effects/VLiquidGlass.h>

namespace vkui::detail {

/** Builds the shared popup material strictly from VLiquidGlassStyle's public controls. */
inline VLiquidGlassStyle popupGlassStyle(const qreal cornerRadius = -1.0) noexcept {
    VLiquidGlassStyle style = VLiquidGlassStyle::regular();
    style.cornerRadius = cornerRadius;
    style.blurRadius = 10.0;
    style.refractionHeight = 0.0;
    style.refractionAmount = 0.0;
    style.chromaticAberration = 0.0;
    style.saturation = 1.02;
    style.tintOpacity = 0.24;
    style.opticalEdgeIntensity = 0.0;
    return style;
}

} // namespace vkui::detail
