// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QSize>
#include <QtCore/QStringView>
#include <QtGui/QFont>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkIcon.h>

namespace vkui {

/** Stable logical layout values for a file-system row. */
struct VKUI_CORE_EXPORT VkFileIconMetrics final {
    QSize iconSize;
    int textGap = 0;
    int rowHeight = 0;
};

/** Calculates file-tree layout metrics from the active interface font. */
[[nodiscard]] VKUI_CORE_EXPORT VkFileIconMetrics fileIconMetrics(const QFont& interfaceFont);

/** Resolves a built-in SVG symbol from a path extension without filesystem I/O. */
[[nodiscard]] VKUI_CORE_EXPORT VkSymbol fileSymbolForPath(QStringView path);

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VkFileIconMetrics)
