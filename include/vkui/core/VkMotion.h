// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QEasingCurve>
#include <QtCore/QMetaType>
#include <vkui/VkUiGlobal.h>

namespace vkui {

enum class VkMotionRole {
    Immediate,
    StateTransition,
    Enter,
    Exit,
    EmphasizedEnter,
    EmphasizedExit,
};

/** Duration and easing policy for a single semantic motion role. */
struct VKUI_CORE_EXPORT VkMotionSpec final {
    int durationMs = 0;
    QEasingCurve easing;
};

/** All motion roles captured by an immutable resolved theme. */
struct VKUI_CORE_EXPORT VkMotionTokens final {
    VkMotionSpec immediate;
    VkMotionSpec stateTransition;
    VkMotionSpec enter;
    VkMotionSpec exit;
    VkMotionSpec emphasizedEnter;
    VkMotionSpec emphasizedExit;
};

/** Returns the current motion policy, honoring the global animation setting. */
[[nodiscard]] VKUI_CORE_EXPORT VkMotionSpec motionSpec(VkMotionRole role);

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VkMotionRole)
Q_DECLARE_METATYPE(vkui::VkMotionSpec)
