// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QString>
#include <vkui/VkUiGlobal.h>

class QApplication;

namespace vkui {

/**
 * Applies an optional QSS file on top of VkStyle.
 *
 * VkUI itself does not require QSS. Applications can opt into a narrow override layer and should
 * use palette roles for colors so one stylesheet follows every appearance without regeneration.
 */
[[nodiscard]] VKUI_WIDGETS_EXPORT bool applyStyleSheetFile(QApplication& application,
                                                           const QString& filePath,
                                                           QString* errorMessage = nullptr);

/** Removes the application QSS overlay while leaving VkStyle installed. */
VKUI_WIDGETS_EXPORT void clearStyleSheet(QApplication& application);

} // namespace vkui
