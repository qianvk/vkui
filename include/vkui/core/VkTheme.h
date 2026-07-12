// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QtTypes>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkColorTokens.h>
#include <vkui/core/VkMetricTokens.h>
#include <vkui/core/VkMotion.h>
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

    [[nodiscard]] VkAppearance effectiveAppearance() const noexcept;
    [[nodiscard]] quint64 generation() const noexcept;

  private:
    VkTheme(VkColorTokens colors, VkMetricTokens metrics, VkTypographyTokens typography,
            VkMotionTokens motion, VkAppearance effectiveAppearance, quint64 generation);

    VkColorTokens colors_;
    VkMetricTokens metrics_;
    VkTypographyTokens typography_;
    VkMotionTokens motion_;
    VkAppearance effectiveAppearance_ = VkAppearance::Light;
    quint64 generation_ = 0;

    friend class VkThemeManagerPrivate;
};

} // namespace vkui
