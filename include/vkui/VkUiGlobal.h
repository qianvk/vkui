// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/qglobal.h>

#if defined(VKUI_STATIC)
#define VKUI_CORE_EXPORT
#define VKUI_WIDGETS_EXPORT
#define VKUI_WINDOW_EXPORT
#else
#if defined(VKUI_BUILDING_CORE)
#define VKUI_CORE_EXPORT Q_DECL_EXPORT
#else
#define VKUI_CORE_EXPORT Q_DECL_IMPORT
#endif
#if defined(VKUI_BUILDING_WIDGETS)
#define VKUI_WIDGETS_EXPORT Q_DECL_EXPORT
#else
#define VKUI_WIDGETS_EXPORT Q_DECL_IMPORT
#endif
#if defined(VKUI_BUILDING_WINDOW)
#define VKUI_WINDOW_EXPORT Q_DECL_EXPORT
#else
#define VKUI_WINDOW_EXPORT Q_DECL_IMPORT
#endif
#endif
