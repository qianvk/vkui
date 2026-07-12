// SPDX-License-Identifier: MIT

#pragma once

#include <QtGui/QFont>
#include <vkui/VkUiGlobal.h>

namespace vkui {

/** Platform-system typography roles for one resolved theme. */
struct VKUI_CORE_EXPORT VkTypographyTokens final {
    QFont caption;
    QFont body;
    QFont bodyEmphasized;
    QFont headline;
    QFont title;
    QFont largeTitle;
};

} // namespace vkui
