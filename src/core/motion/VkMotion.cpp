// SPDX-License-Identifier: MIT

#include <vkui/core/VkMotion.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>

namespace vkui {

VkMotionSpec motionSpec(const VkMotionRole role) {
    const VkThemeManager* manager = VkThemeManager::instance();
    const VkMotionTokens& motion = manager->theme().motion();

    VkMotionSpec result;
    switch (role) {
    case VkMotionRole::Immediate:
        result = motion.immediate;
        break;
    case VkMotionRole::StateTransition:
        result = motion.stateTransition;
        break;
    case VkMotionRole::Enter:
        result = motion.enter;
        break;
    case VkMotionRole::Exit:
        result = motion.exit;
        break;
    case VkMotionRole::EmphasizedEnter:
        result = motion.emphasizedEnter;
        break;
    case VkMotionRole::EmphasizedExit:
        result = motion.emphasizedExit;
        break;
    }

    if (!manager->animationsEnabled()) {
        result.durationMs = 0;
    }
    return result;
}

} // namespace vkui
