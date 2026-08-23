// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMetaType>
#include <QtGui/QFont>
#include <optional>
#include <vkui/VkUiGlobal.h>

class QWidget;

namespace vkui {

/** Semantic interface-text roles that preserve hierarchy across text-scale changes. */
enum class VTextStyle {
    Caption,
    Body,
    BodyEmphasized,
    Headline,
    Title,
    LargeTitle,
};

/** Resolves a semantic text role from the current immutable theme. */
[[nodiscard]] VKUI_WIDGETS_EXPORT QFont textStyleFont(VTextStyle style);

/** Returns the semantic role bound to a widget, or no value when it inherits normally. */
[[nodiscard]] VKUI_WIDGETS_EXPORT std::optional<VTextStyle>
textStyle(const QWidget& widget) noexcept;

/** Binds a widget to a semantic role and updates it only when typography changes. */
VKUI_WIDGETS_EXPORT void setTextStyle(QWidget& widget, VTextStyle style);

/** Removes the semantic role and restores normal application-font inheritance. */
VKUI_WIDGETS_EXPORT void resetTextStyle(QWidget& widget);

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VTextStyle)
