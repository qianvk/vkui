// SPDX-License-Identifier: MIT

#include "private/VkSvgIconEngine_p.h"

#include <QtCore/QResource>
#include <vkui/core/VkIcon.h>

static void vkuiEnsureIconResourcesInitialized() {
    // Referencing the generated initializer keeps the resource object linked
    // when VkUI::Core itself is built as a static library.
    Q_INIT_RESOURCE(vkui);
}

namespace vkui {

QIcon icon(const VkSymbol symbol, const VkIconRole role) {
    static const bool initialized = [] {
        vkuiEnsureIconResourcesInitialized();
        return true;
    }();
    Q_UNUSED(initialized);
    return QIcon(new VkSvgIconEngine(symbol, role));
}

} // namespace vkui
