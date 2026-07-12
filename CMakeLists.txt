# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)

project(
    VkUI
    VERSION 0.1.0
    DESCRIPTION "A restrained macOS-inspired visual foundation for Qt Widgets"
    HOMEPAGE_URL "https://github.com/vkui-project/vkui"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(VKUI_BUILD_SHARED "Build VkUI as shared libraries" ON)
option(VKUI_BUILD_EXAMPLES "Build the component gallery" ON)
option(VKUI_BUILD_TESTS "Build unit tests" ON)
option(VKUI_INSTALL "Generate installation rules" ON)
option(VKUI_ENABLE_WARNINGS "Enable additional compiler warnings" ON)
option(VKUI_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(VKUI_EXPORT_COMPILE_COMMANDS "Generate a clangd compilation database" ON)
option(VKUI_SYNC_COMPILE_COMMANDS
       "Refresh compile_commands.json in the source root for clangd and Neovim"
       ON)

if(VKUI_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Svg)
if(VKUI_BUILD_EXAMPLES)
    find_package(Qt6 6.6 REQUIRED COMPONENTS LinguistTools)
endif()

include(cmake/VkUICompilerWarnings.cmake)
include(cmake/VkUISanitizers.cmake)

if(VKUI_BUILD_SHARED)
    set(_vkui_library_type SHARED)
else()
    set(_vkui_library_type STATIC)
endif()

add_library(vkui_core ${_vkui_library_type})
add_library(VkUI::Core ALIAS vkui_core)

target_sources(vkui_core
    PRIVATE
        include/vkui/VkUiGlobal.h
        include/vkui/Core.h
        include/vkui/core/VkAccentColor.h
        include/vkui/core/VkAppearance.h
        include/vkui/core/VkTheme.h
        include/vkui/core/VkThemeManager.h
        include/vkui/core/VkColorTokens.h
        include/vkui/core/VkMetricTokens.h
        include/vkui/core/VkTypographyTokens.h
        include/vkui/core/VkMotion.h
        include/vkui/core/VkIcon.h
        src/core/theme/VkTheme.cpp
        src/core/theme/VkThemeManager.cpp
        src/core/theme/private/VkThemeManager_p.h
        src/core/icons/VkIcon.cpp
        src/core/icons/private/VkSvgIconEngine_p.h
        src/core/icons/private/VkSvgIconEngine.cpp
        src/core/icons/private/VkIconCache_p.h
        src/core/icons/private/VkIconCache.cpp
        src/core/motion/VkMotion.cpp
        resources/vkui.qrc
)

target_compile_definitions(vkui_core PRIVATE VKUI_BUILDING_CORE)
target_include_directories(vkui_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(vkui_core PUBLIC Qt6::Core Qt6::Gui Qt6::Svg)
set_target_properties(vkui_core PROPERTIES
    EXPORT_NAME Core
    OUTPUT_NAME vkui-core
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

add_library(vkui_widgets ${_vkui_library_type})
add_library(VkUI::Widgets ALIAS vkui_widgets)

target_sources(vkui_widgets
    PRIVATE
        include/vkui/Widgets.h
        include/vkui/widgets/VkComboBox.h
        include/vkui/widgets/VkControlSize.h
        include/vkui/widgets/style/VkStyle.h
        include/vkui/widgets/style/VkStyleSheet.h
        include/vkui/widgets/controls/VkSwitch.h
        include/vkui/widgets/controls/VkSegmentedControl.h
        include/vkui/widgets/overlays/VkPopover.h
        src/widgets/style/VkStyle.cpp
        src/widgets/style/VkStyleSheet.cpp
        src/widgets/VkComboBox.cpp
        src/widgets/style/VkControlSize.cpp
        src/widgets/style/private/VkStyle_p.h
        src/widgets/style/private/VkStylePainter_p.h
        src/widgets/style/private/VkStylePainter.cpp
        src/widgets/style/private/VkWidgetAnimationManager_p.h
        src/widgets/style/private/VkWidgetAnimationManager.cpp
        src/widgets/controls/VkSwitch.cpp
        src/widgets/controls/VkSegmentedControl.cpp
        src/widgets/controls/private/VkSwitch_p.h
        src/widgets/controls/private/VkSegmentedControl_p.h
        src/widgets/overlays/VkPopover.cpp
        src/widgets/overlays/private/VkPopover_p.h
        src/widgets/overlays/private/VkPopoverPlacementEngine_p.h
        src/widgets/overlays/private/VkPopoverPlacementEngine.cpp
        src/widgets/overlays/private/VkPopoverPath_p.h
        src/widgets/overlays/private/VkPopoverPath.cpp
        src/widgets/overlays/private/VkPopoverShadowCache_p.h
        src/widgets/overlays/private/VkPopoverShadowCache.cpp
        src/widgets/overlays/private/VkPopoverAnimation_p.h
        src/widgets/overlays/private/VkPopoverAnimation.cpp
)

target_compile_definitions(vkui_widgets PRIVATE VKUI_BUILDING_WIDGETS)
target_include_directories(vkui_widgets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(vkui_widgets PUBLIC VkUI::Core Qt6::Widgets)
set_target_properties(vkui_widgets PROPERTIES
    EXPORT_NAME Widgets
    OUTPUT_NAME vkui-widgets
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

if(NOT VKUI_BUILD_SHARED)
    target_compile_definitions(vkui_core PUBLIC VKUI_STATIC)
    target_compile_definitions(vkui_widgets PUBLIC VKUI_STATIC)
endif()

if(VKUI_ENABLE_WARNINGS)
    vkui_enable_compiler_warnings(vkui_core)
    vkui_enable_compiler_warnings(vkui_widgets)
endif()

if(VKUI_ENABLE_SANITIZERS)
    vkui_enable_sanitizers(vkui_core)
    vkui_enable_sanitizers(vkui_widgets)
endif()

if(VKUI_BUILD_EXAMPLES)
    add_subdirectory(examples/gallery)
endif()

if(VKUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PROJECT_IS_TOP_LEVEL
   AND VKUI_EXPORT_COMPILE_COMMANDS
   AND VKUI_SYNC_COMPILE_COMMANDS
   AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    add_custom_target(vkui_clangd_database ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/compile_commands.json
                ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMENT "Refreshing clangd compilation database: ${CMAKE_SOURCE_DIR}/compile_commands.json"
        VERBATIM
    )
    message(STATUS "clangd compilation database: ${CMAKE_SOURCE_DIR}/compile_commands.json")
endif()

if(VKUI_INSTALL)
    install(TARGETS vkui_core
        EXPORT VkUICoreTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(TARGETS vkui_widgets
        EXPORT VkUIWidgetsTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(DIRECTORY include/vkui DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT VkUICoreTargets
        FILE VkUICoreTargets.cmake
        NAMESPACE VkUI::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VkUI
    )
    install(EXPORT VkUIWidgetsTargets
        FILE VkUIWidgetsTargets.cmake
        NAMESPACE VkUI::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VkUI
    )

    configure_package_config_file(
        cmake/VkUIConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/VkUIConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VkUI
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/VkUIConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/VkUIConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/VkUIConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VkUI
    )
    install(FILES LICENSE README.md CHANGELOG.md THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/vkui
    )
    install(DIRECTORY resources/translations/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/vkui/translations
        FILES_MATCHING PATTERN "*.ts"
    )
endif()
