# SPDX-License-Identifier: MIT

# VkUI owns the active native-window implementation. The retained QWindowKit
# source tree is reference material and is intentionally not part of this
# target's source, include, or link graph.

set(_vkui_window_sources
    include/vkui/Window.h
    include/vkui/window/VMessageDialog.h
    include/vkui/window/VSystemButton.h
    include/vkui/window/VWindowAgent.h
    src/window/VMessageDialog.cpp
    src/window/VWindowAgent.cpp
    src/window/native/VNativeWindowController_p.h
    src/window/native/VNativeWindowController.cpp
    src/window/native/VPlatformWindowBackend_p.h
)

set(_vkui_window_platform_libraries)
if(APPLE)
    list(APPEND _vkui_window_sources
        src/window/native/VNativeWindowBackend_mac.mm)
    list(APPEND _vkui_window_platform_libraries
        "-framework AppKit")
elseif(WIN32)
    list(APPEND _vkui_window_sources
        src/window/native/VNativeWindowBackend_win.cpp)
    list(APPEND _vkui_window_platform_libraries
        comctl32
        dwmapi
        user32)
else()
    message(FATAL_ERROR
        "VkUI::Window currently supports only macOS and Windows. "
        "Configure with VKUI_BUILD_WINDOW=OFF on ${CMAKE_SYSTEM_NAME}.")
endif()

add_library(vkui_window ${_vkui_library_type} ${_vkui_window_sources})
add_library(VkUI::Window ALIAS vkui_window)
target_compile_definitions(vkui_window PRIVATE VKUI_BUILDING_WINDOW)
target_include_directories(vkui_window
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/window/native
)
target_link_libraries(vkui_window
    PUBLIC
        VkUI::Core
        Qt6::Widgets
    PRIVATE
        ${_vkui_window_platform_libraries}
)
set_target_properties(vkui_window PROPERTIES
    EXPORT_NAME Window
    OUTPUT_NAME vkui-window
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
if(VKUI_BUILD_SHARED)
    set_target_properties(vkui_window PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        OBJCXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
endif()
