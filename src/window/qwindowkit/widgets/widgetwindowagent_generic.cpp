// Copyright (C) 2023-2024 Stdware Collections (https://www.github.com/stdware)
// Copyright (C) 2021-2023 wangwenx190 (Yuhang Zhao)
// SPDX-License-Identifier: Apache-2.0

#include "widgetwindowagent_p.h"

namespace QWK {

    bool WidgetWindowAgentPrivate::installPlatformSystemButtons() {
        return false;
    }

    QRect WidgetWindowAgentPrivate::platformSystemButtonAreaGeometry() const {
        return {};
    }

}
