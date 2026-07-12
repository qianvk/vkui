// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/Qt>
#include <vkui/VkUiGlobal.h>

class QComboBox;

namespace vkui {

/** Sets how constrained, non-editable combo-box labels are elided. */
VKUI_WIDGETS_EXPORT void setComboBoxElideMode(QComboBox& comboBox, Qt::TextElideMode mode);

/** Returns the configured label elide mode. The default is Qt::ElideRight. */
[[nodiscard]] VKUI_WIDGETS_EXPORT Qt::TextElideMode comboBoxElideMode(const QComboBox& comboBox);

} // namespace vkui
