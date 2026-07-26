// SPDX-License-Identifier: MIT

#pragma once

#include <vkui/VkUiGlobal.h>

namespace vkui::detail {

/**
 * Registers VkUI's compiled resources from inside the Core library.
 *
 * Keeping the generated qInitResources symbol behind this exported function
 * avoids crossing a shared-library boundary on Windows.
 */
VKUI_CORE_EXPORT void ensureResourcesInitialized();

} // namespace vkui::detail
