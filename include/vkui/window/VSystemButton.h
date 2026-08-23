// SPDX-License-Identifier: MIT

#pragma once

#include <QFlags>
#include <QMetaType>
#include <QtGlobal>

namespace vkui {

/** Platform-owned buttons that may be requested for a native window. */
enum class VSystemButton : quint8 {
    Close = 0x01,
    Minimize = 0x02,
    Maximize = 0x04,
};
Q_DECLARE_FLAGS(VSystemButtons, VSystemButton)
Q_DECLARE_OPERATORS_FOR_FLAGS(VSystemButtons)

inline constexpr VSystemButtons VStandardSystemButtons =
    VSystemButton::Close | VSystemButton::Minimize | VSystemButton::Maximize;

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VSystemButtons)
