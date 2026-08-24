// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QtTypes>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkColorTokens.h>
#include <vkui/core/VkMetricTokens.h>
#include <vkui/core/VkMotion.h>
#include <vkui/core/VkTextSize.h>
#include <vkui/core/VkTypographyTokens.h>

namespace vkui {

class VkThemeManagerPrivate;

/** An immutable collection of tokens resolved for a concrete appearance. */
class VKUI_CORE_EXPORT VkTheme final {
  public:
    VkTheme(const VkTheme& other);
    VkTheme(VkTheme&& other) noexcept;
    VkTheme& operator=(const VkTheme& other);
    VkTheme& operator=(VkTheme&& other) noexcept;
    ~VkTheme();

    [[nodiscard]] const VkColorTokens& colors() const noexcept;
    [[nodiscard]] const VkMetricTokens& metrics() const noexcept;
    [[nodiscard]] const VkTypographyTokens& typography() const noexcept;
    [[nodiscard]] const VkMotionTokens& motion() const noexcept;
    /** Discrete interface-text level used to resolve this theme. */
    [[nodiscard]] int textSizeLevel() const noexcept;
    /** Relative interface-text scale used to resolve typography and responsive geometry. */
    [[nodiscard]] qreal textScale() const noexcept;

    [[nodiscard]] VkAppearance effectiveAppearance() const noexcept;
    /** Increments whenever any resolved token group changes. */
    [[nodiscard]] quint64 generation() const noexcept;
    /** Increments only when resolved colors change. Suitable for raster cache keys. */
    [[nodiscard]] quint64 colorGeneration() const noexcept;

  private:
    VkTheme(VkColorTokens colors, VkMetricTokens metrics, VkTypographyTokens typography,
            VkMotionTokens motion, int textSizeLevel, qreal textScale,
            VkAppearance effectiveAppearance, quint64 generation, quint64 colorGeneration);

    VkColorTokens colors_;
    VkMetricTokens metrics_;
    VkTypographyTokens typography_;
    VkMotionTokens motion_;
    int textSizeLevel_ = VkDefaultTextSizeLevel;
    qreal textScale_ = 1.0;
    VkAppearance effectiveAppearance_ = VkAppearance::Light;
    quint64 generation_ = 0;
    quint64 colorGeneration_ = 0;

    friend class VkThemeManagerPrivate;
};

} // namespace vkui
