// SPDX-License-Identifier: MIT

#include "private/VkResourceInitializer_p.h"
#include "private/VkSvgIconEngine_p.h"

#include <QtCore/QResource>
#include <vkui/core/VkIcon.h>

static void initializeVkUiResources() {
    // Referencing the generated initializer keeps the resource object linked
    // when VkUI::Core itself is built as a static library.
    Q_INIT_RESOURCE(vkui);
}

namespace vkui::detail {

void ensureResourcesInitialized() {
    static const bool initialized = [] {
        initializeVkUiResources();
        return true;
    }();
    Q_UNUSED(initialized);
}

} // namespace vkui::detail

namespace vkui {

QIcon icon(const VkSymbol symbol, const VkIconRole role) {
    if (symbol < VkSymbol::ChevronLeft || symbol >= VkSymbol::Count) {
        return {};
    }
    detail::ensureResourcesInitialized();
    return QIcon(new VkSvgIconEngine(symbol, role));
}

QIcon icon(const VkSymbol symbol, const QPalette::ColorRole role,
           const QPalette::ColorGroup group) {
    if (symbol < VkSymbol::ChevronLeft || symbol >= VkSymbol::Count) {
        return {};
    }
    detail::ensureResourcesInitialized();
    return QIcon(new VkSvgIconEngine(symbol, role, group));
}

QIcon icon(const VkSymbol symbol, const QColor& primary, const QColor& secondary) {
    if (symbol < VkSymbol::ChevronLeft || symbol >= VkSymbol::Count || !primary.isValid()) {
        return {};
    }
    detail::ensureResourcesInitialized();
    return QIcon(new VkSvgIconEngine(symbol, primary, secondary));
}

} // namespace vkui
