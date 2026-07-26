# SPDX-License-Identifier: MIT

# The window module is built directly from the migrated implementation. It
# intentionally does not invoke QWindowKit's build system or qmsetup and does
# not expose either as a package dependency.

set(_vkui_window_impl "${CMAKE_CURRENT_SOURCE_DIR}/src/window/qwindowkit")
set(_vkui_window_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/vkui-window")

set(VKUI_WINDOW_FORCE_QT_CONTEXT_VALUE -1)
if(VKUI_WINDOW_FORCE_QT_CONTEXT)
    set(VKUI_WINDOW_FORCE_QT_CONTEXT_VALUE 1)
endif()
set(VKUI_WINDOW_ENABLE_STYLE_AGENT_VALUE -1)
if(VKUI_WINDOW_ENABLE_STYLE_AGENT)
    set(VKUI_WINDOW_ENABLE_STYLE_AGENT_VALUE 1)
endif()
set(VKUI_WINDOW_WINDOWS_BORDERS_VALUE -1)
if(VKUI_WINDOW_ENABLE_WINDOWS_SYSTEM_BORDERS)
    set(VKUI_WINDOW_WINDOWS_BORDERS_VALUE 1)
endif()
configure_file(
    "${_vkui_window_impl}/core/qwkconfig.h.in"
    "${_vkui_window_generated}/qwkconfig.h"
)

set(_vkui_window_sources
    include/vkui/Window.h
    include/vkui/window/VkFramelessDialog.h
    include/vkui/window/VkMessageDialog.h
    include/vkui/window/VkWindowAgent.h
    src/window/VkFramelessDialog.cpp
    src/window/VkMessageDialog.cpp
    src/window/VkWindowAgent.cpp

    ${_vkui_window_impl}/core/qwkglobal.h
    ${_vkui_window_impl}/core/qwkglobal_p.h
    ${_vkui_window_impl}/core/qwkglobal.cpp
    ${_vkui_window_impl}/core/windowagentbase.h
    ${_vkui_window_impl}/core/windowagentbase_p.h
    ${_vkui_window_impl}/core/windowagentbase.cpp
    ${_vkui_window_impl}/core/windowitemdelegate_p.h
    ${_vkui_window_impl}/core/windowitemdelegate.cpp
    ${_vkui_window_impl}/core/kernel/nativeeventfilter_p.h
    ${_vkui_window_impl}/core/kernel/nativeeventfilter.cpp
    ${_vkui_window_impl}/core/kernel/sharedeventfilter_p.h
    ${_vkui_window_impl}/core/kernel/sharedeventfilter.cpp
    ${_vkui_window_impl}/core/kernel/winidchangeeventfilter_p.h
    ${_vkui_window_impl}/core/kernel/winidchangeeventfilter.cpp
    ${_vkui_window_impl}/core/shared/systemwindow_p.h
    ${_vkui_window_impl}/core/contexts/abstractwindowcontext_p.h
    ${_vkui_window_impl}/core/contexts/abstractwindowcontext.cpp
    ${_vkui_window_impl}/core/contexts/qtwindowcontext_p.h
    ${_vkui_window_impl}/core/contexts/qtwindowcontext.cpp

    ${_vkui_window_impl}/widgets/qwkwidgetsglobal.h
    ${_vkui_window_impl}/widgets/widgetitemdelegate_p.h
    ${_vkui_window_impl}/widgets/widgetitemdelegate.cpp
    ${_vkui_window_impl}/widgets/widgetwindowagent.h
    ${_vkui_window_impl}/widgets/widgetwindowagent_p.h
    ${_vkui_window_impl}/widgets/widgetwindowagent.cpp
)

if(VKUI_WINDOW_ENABLE_STYLE_AGENT)
    list(APPEND _vkui_window_sources
        ${_vkui_window_impl}/core/style/styleagent.h
        ${_vkui_window_impl}/core/style/styleagent_p.h
        ${_vkui_window_impl}/core/style/styleagent.cpp
    )
endif()

set(_vkui_window_platform_libraries)
if(WIN32)
    list(APPEND _vkui_window_sources
        ${_vkui_window_impl}/core/qwindowkit_windows.h
        ${_vkui_window_impl}/core/qwindowkit_windows.cpp
        ${_vkui_window_impl}/core/shared/qwkwindowsextra_p.h
        ${_vkui_window_impl}/core/shared/windows10borderhandler_p.h
        ${_vkui_window_impl}/core/contexts/win32windowcontext_p.h
        ${_vkui_window_impl}/core/contexts/win32windowcontext.cpp
        ${_vkui_window_impl}/widgets/widgetwindowagent_nonmac.cpp
        ${_vkui_window_impl}/widgets/widgetwindowagent_win.cpp
        ${_vkui_window_impl}/widgets/resources/windows_caption_buttons.qrc
    )
    if(VKUI_WINDOW_ENABLE_STYLE_AGENT)
        list(APPEND _vkui_window_sources
            ${_vkui_window_impl}/core/style/styleagent_win.cpp)
    endif()
    list(APPEND _vkui_window_platform_libraries uxtheme)
elseif(APPLE)
    list(APPEND _vkui_window_sources
        ${_vkui_window_impl}/core/contexts/cocoawindowcontext_p.h
        ${_vkui_window_impl}/core/contexts/cocoawindowcontext.mm
        ${_vkui_window_impl}/widgets/widgetwindowagent_mac.cpp
    )
    if(VKUI_WINDOW_ENABLE_STYLE_AGENT)
        list(APPEND _vkui_window_sources
            ${_vkui_window_impl}/core/style/styleagent_mac.mm)
    endif()
    list(APPEND _vkui_window_platform_libraries
        "-framework Foundation"
        "-framework Cocoa"
        "-framework AppKit"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _vkui_window_sources
        ${_vkui_window_impl}/core/qwindowkit_linux.h
        ${_vkui_window_impl}/core/qwindowkit_linux.cpp
        ${_vkui_window_impl}/core/contexts/linuxx11context_p.h
        ${_vkui_window_impl}/core/contexts/linuxx11context.cpp
        ${_vkui_window_impl}/core/contexts/linuxwaylandcontext_p.h
        ${_vkui_window_impl}/core/contexts/linuxwaylandcontext.cpp
        ${_vkui_window_impl}/widgets/widgetwindowagent_nonmac.cpp
        ${_vkui_window_impl}/widgets/widgetwindowagent_generic.cpp
    )
    if(VKUI_WINDOW_ENABLE_STYLE_AGENT)
        list(APPEND _vkui_window_sources
            ${_vkui_window_impl}/core/style/styleagent_linux.cpp)
    endif()
else()
    message(FATAL_ERROR "VkUI::Window does not support ${CMAKE_SYSTEM_NAME}.")
endif()

add_library(vkui_window ${_vkui_library_type} ${_vkui_window_sources})
add_library(VkUI::Window ALIAS vkui_window)
target_compile_definitions(vkui_window
    PRIVATE
        VKUI_BUILDING_WINDOW
)
if(VKUI_BUILD_SHARED AND VKUI_BUILD_WINDOW_QUICK)
    # WindowQuick derives from the migrated core classes in this library.
    # Export only its core dependency across the shared-library boundary;
    # widget implementation classes remain private to VkUI::Window.
    target_compile_definitions(vkui_window
        PRIVATE QWK_CORE_LIBRARY QWK_WIDGETS_STATIC)
else()
    target_compile_definitions(vkui_window
        PRIVATE QWK_CORE_STATIC QWK_WIDGETS_STATIC)
endif()
target_include_directories(vkui_window
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${_vkui_window_generated}
        ${_vkui_window_impl}/core
        ${_vkui_window_impl}/core/contexts
        ${_vkui_window_impl}/core/kernel
        ${_vkui_window_impl}/core/shared
        ${_vkui_window_impl}/core/style
        ${_vkui_window_impl}/widgets
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
    # The embedded implementation stays outside VkUI's public ABI. Explicit
    # export annotations retain the facade and the core symbols that an
    # optional WindowQuick shared library imports.
    set_target_properties(vkui_window PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        OBJCXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
endif()

foreach(_vkui_private_component Core Gui Widgets)
    if(NOT TARGET Qt6::${_vkui_private_component}Private)
        # Qt 6.8 and earlier expose private targets from the public module
        # package, while newer SDKs ship sibling *Private packages. Search only
        # next to the selected Qt SDK so another system Qt can never leak in.
        get_filename_component(
            _vkui_qt_cmake_root
            "${Qt6${_vkui_private_component}_DIR}/.."
            ABSOLUTE)
        set(QT_NO_PRIVATE_MODULE_WARNING ON)
        find_package(
            Qt6${_vkui_private_component}Private ${Qt6_VERSION} EXACT
            CONFIG QUIET
            PATHS
                "${_vkui_qt_cmake_root}/Qt6${_vkui_private_component}Private"
            NO_DEFAULT_PATH)
    endif()
    if(TARGET Qt6::${_vkui_private_component}Private)
        target_link_libraries(vkui_window
            PRIVATE Qt6::${_vkui_private_component}Private)
    endif()
endforeach()

set(_vkui_missing_private_targets)
foreach(_vkui_private_component Core Gui Widgets)
    if(NOT TARGET Qt6::${_vkui_private_component}Private)
        list(APPEND _vkui_missing_private_targets
            "Qt6::${_vkui_private_component}Private")
    endif()
endforeach()
if(_vkui_missing_private_targets)
    list(JOIN _vkui_missing_private_targets ", " _vkui_missing_private_targets_text)
    message(FATAL_ERROR
        "VkUI::Window requires Qt private development targets: "
        "${_vkui_missing_private_targets_text}.")
endif()

if(VKUI_BUILD_WINDOW_QUICK)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Qml Quick)
    if(NOT TARGET Qt6::QuickPrivate)
        get_filename_component(
            _vkui_qt_cmake_root
            "${Qt6Quick_DIR}/.."
            ABSOLUTE)
        find_package(
            Qt6QuickPrivate ${Qt6_VERSION} EXACT CONFIG QUIET
            PATHS "${_vkui_qt_cmake_root}/Qt6QuickPrivate"
            NO_DEFAULT_PATH)
    endif()
    if(NOT TARGET Qt6::QuickPrivate)
        message(FATAL_ERROR
            "The internal vkui_window_quick target requires the "
            "Qt6::QuickPrivate development target.")
    endif()

    set(_vkui_window_quick_sources
        ${_vkui_window_impl}/quick/qwkquickglobal.h
        ${_vkui_window_impl}/quick/qwkquickglobal.cpp
        ${_vkui_window_impl}/quick/quickitemdelegate_p.h
        ${_vkui_window_impl}/quick/quickitemdelegate.cpp
        ${_vkui_window_impl}/quick/quickwindowagent.h
        ${_vkui_window_impl}/quick/quickwindowagent_p.h
        ${_vkui_window_impl}/quick/quickwindowagent.cpp
    )
    if(WIN32)
        list(APPEND _vkui_window_quick_sources
            ${_vkui_window_impl}/quick/quickwindowagent_win.cpp)
    elseif(APPLE)
        list(APPEND _vkui_window_quick_sources
            ${_vkui_window_impl}/quick/quickwindowagent_mac.cpp)
    endif()

    add_library(vkui_window_quick ${_vkui_library_type}
        ${_vkui_window_quick_sources})
    if(VKUI_BUILD_SHARED)
        target_compile_definitions(vkui_window_quick
            PRIVATE QWK_QUICK_LIBRARY)
    else()
        target_compile_definitions(vkui_window_quick
            PRIVATE QWK_CORE_STATIC QWK_QUICK_STATIC)
    endif()
    target_include_directories(vkui_window_quick
        PRIVATE
            ${_vkui_window_generated}
            ${_vkui_window_impl}/core
            ${_vkui_window_impl}/core/contexts
            ${_vkui_window_impl}/core/kernel
            ${_vkui_window_impl}/core/shared
            ${_vkui_window_impl}/quick
    )
    target_link_libraries(vkui_window_quick
        PUBLIC VkUI::Window Qt6::Qml Qt6::Quick)
    if(TARGET Qt6::QuickPrivate)
        target_link_libraries(vkui_window_quick PRIVATE Qt6::QuickPrivate)
    endif()
    set_target_properties(vkui_window_quick PROPERTIES
        OUTPUT_NAME vkui-window-quick
        VERSION ${PROJECT_VERSION}
        SOVERSION 0
    )
endif()
