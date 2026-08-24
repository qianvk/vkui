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

if(APPLE OR WIN32)
    set(_vkui_native_window_default ON)
else()
    set(_vkui_native_window_default OFF)
endif()

option(VKUI_BUILD_SHARED "Build VkUI as shared libraries" ON)
option(VKUI_BUILD_EXAMPLES "Build the component gallery"
       ${_vkui_native_window_default})
option(VKUI_BUILD_ICON_CHOSEN "Build the temporary SVG icon selection tool"
       ${VKUI_BUILD_EXAMPLES})
option(VKUI_BUILD_TESTS "Build unit tests" ON)
option(VKUI_INSTALL "Generate installation rules" ON)
option(VKUI_BUILD_WINDOW "Build the macOS/Windows native window module"
       ${_vkui_native_window_default})
option(VKUI_BUILD_INTERACTION
       "Build the modal interaction, buffer, panel, and widget adapters" ON)
option(VKUI_ENABLE_WARNINGS "Enable additional compiler warnings" ON)
option(VKUI_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(VKUI_EXPORT_COMPILE_COMMANDS "Generate a clangd compilation database" ON)
option(VKUI_SYNC_COMPILE_COMMANDS
       "Refresh compile_commands.json in the source root for clangd and Neovim"
       ON)

if(VKUI_BUILD_EXAMPLES AND NOT VKUI_BUILD_WINDOW)
    message(FATAL_ERROR
        "VKUI_BUILD_EXAMPLES requires VKUI_BUILD_WINDOW because the component "
        "gallery demonstrates VkUI's integrated frameless window.")
endif()

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
# Windows resolves project DLLs beside each executable. A single runtime
# directory also gives tests and the gallery one predictable launch location.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Svg)
if(VKUI_BUILD_INTERACTION)
    if(APPLE AND NOT ICU_ROOT)
        foreach(_vkui_icu_root
                IN ITEMS /opt/homebrew/opt/icu4c /usr/local/opt/icu4c)
            if(EXISTS "${_vkui_icu_root}/include/unicode/utf16.h")
                set(ICU_ROOT "${_vkui_icu_root}")
                break()
            endif()
        endforeach()
    endif()
    find_package(ICU 70 REQUIRED COMPONENTS uc i18n)
endif()
if(VKUI_BUILD_EXAMPLES)
    find_package(Qt6LinguistTools 6.6 QUIET)
    if(NOT Qt6LinguistTools_FOUND)
        get_filename_component(_vkui_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
        find_program(VKUI_LRELEASE_EXECUTABLE
            NAMES lrelease-qt6 lrelease6 lrelease
            HINTS
                "${_vkui_qt_prefix}/bin"
                "$ENV{QTDIR}/bin"
        )
        if(NOT VKUI_LRELEASE_EXECUTABLE)
            file(GLOB _vkui_lrelease_candidates LIST_DIRECTORIES FALSE
                "$ENV{HOME}/Qt/*/macos/bin/lrelease"
                "$ENV{HOME}/Qt/*/gcc_64/bin/lrelease"
                "$ENV{HOME}/Qt/*/mingw_64/bin/lrelease.exe"
                "$ENV{HOME}/Qt/*/msvc*/bin/lrelease.exe"
            )
            if(_vkui_lrelease_candidates)
                list(SORT _vkui_lrelease_candidates COMPARE NATURAL ORDER DESCENDING)
                list(GET _vkui_lrelease_candidates 0 VKUI_LRELEASE_EXECUTABLE)
            endif()
        endif()
        if(VKUI_LRELEASE_EXECUTABLE)
            message(STATUS "Using lrelease fallback: ${VKUI_LRELEASE_EXECUTABLE}")
        else()
            message(WARNING
                "Qt LinguistTools and lrelease are unavailable; Gallery translations "
                "will not be compiled")
        endif()
    endif()
endif()

include(cmake/VkUICompilerWarnings.cmake)
include(cmake/VkUISanitizers.cmake)
include(cmake/VkUIIcons.cmake)

vkui_generate_icon_catalog(
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/manifest.json"
    ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/icons"
)

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
        include/vkui/core/VkThemeChange.h
        include/vkui/core/VkTextSize.h
        include/vkui/core/VkLiquidGlassPreference.h
        include/vkui/core/VkThemeManager.h
        include/vkui/core/VkColorTokens.h
        include/vkui/core/VkDiagnostics.h
        include/vkui/core/VkMetricTokens.h
        include/vkui/core/VkTypographyTokens.h
        include/vkui/core/VkMotion.h
        include/vkui/core/VkIcon.h
        ${VKUI_SYMBOL_HEADER}
        include/vkui/core/VkFileIcon.h
        src/core/theme/VkTheme.cpp
        src/core/diagnostics/VkDiagnostics.cpp
        src/core/theme/VkThemeManager.cpp
        src/core/theme/private/VkThemeManager_p.h
        src/core/icons/VkIcon.cpp
        src/core/icons/VkFileIcon.cpp
        src/core/icons/private/VkResourceInitializer_p.h
        src/core/icons/private/VkSvgIconEngine_p.h
        src/core/icons/private/VkSvgIconEngine.cpp
        src/core/icons/private/VkSvgIconSourceCache_p.h
        src/core/icons/private/VkSvgIconSourceCache.cpp
        ${VKUI_SYMBOL_REGISTRY_HEADER}
        src/core/icons/private/VkIconCache_p.h
        src/core/icons/private/VkIconCache.cpp
        src/core/icons/private/VkIconMaskCache_p.h
        src/core/icons/private/VkIconMaskCache.cpp
        src/core/motion/VkMotion.cpp
        ${VKUI_ICON_QRC}
)

target_compile_definitions(vkui_core PRIVATE VKUI_BUILDING_CORE)
target_include_directories(vkui_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${VKUI_GENERATED_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${VKUI_GENERATED_PRIVATE_DIR}
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
        include/vkui/widgets/VCombobox.h
        include/vkui/widgets/VControlSize.h
        include/vkui/widgets/VTextStyle.h
        include/vkui/widgets/style/VStyle.h
        include/vkui/widgets/controls/VSplitter.h
        include/vkui/widgets/controls/VSlider.h
        include/vkui/widgets/controls/VSwitch.h
        include/vkui/widgets/controls/VSegmentedControl.h
        include/vkui/widgets/effects/VLiquidGlass.h
        include/vkui/widgets/overlays/VPopover.h
        include/vkui/widgets/views/VTreeItemDelegate.h
        include/vkui/widgets/views/VTreeView.h
        include/vkui/widgets/views/VFileTreeView.h
        src/widgets/style/VStyle.cpp
        src/widgets/VCombobox.cpp
        src/widgets/style/VControlSize.cpp
        src/widgets/style/VTextStyle.cpp
        src/widgets/animation/private/VkWidgetAnimation_p.h
        src/widgets/animation/private/VkWidgetAnimation.cpp
        src/widgets/style/private/VStyle_p.h
        src/widgets/style/private/VkPopupSurfaceStyler_p.h
        src/widgets/style/private/VkPopupSurfaceStyler.cpp
        src/widgets/style/private/VkScrollBarActivityController_p.h
        src/widgets/style/private/VkScrollBarActivityController.cpp
        src/widgets/style/private/VkThemeRefreshCoordinator_p.h
        src/widgets/style/private/VkThemeRefreshCoordinator.cpp
        src/widgets/style/private/VkWidgetTypographyController_p.h
        src/widgets/style/private/VkWidgetTypographyController.cpp
        src/widgets/style/private/VStylePainter_p.h
        src/widgets/style/private/VStylePainter.cpp
        src/widgets/controls/VSplitter.cpp
        src/widgets/controls/VSlider.cpp
        src/widgets/controls/VSwitch.cpp
        src/widgets/controls/VSegmentedControl.cpp
        src/widgets/controls/private/VSwitch_p.h
        src/widgets/controls/private/VSegmentedControl_p.h
        src/widgets/effects/VLiquidGlass.cpp
        src/widgets/effects/private/VkLiquidGlassRenderer_p.h
        src/widgets/effects/private/VkLiquidGlassRenderer.cpp
        src/widgets/effects/private/VkShadowCache_p.h
        src/widgets/effects/private/VkShadowCache.cpp
        src/widgets/overlays/VPopover.cpp
        src/widgets/overlays/private/VPopover_p.h
        src/widgets/overlays/private/VPopoverPlacementEngine_p.h
        src/widgets/overlays/private/VPopoverPlacementEngine.cpp
        src/widgets/overlays/private/VPopoverPath_p.h
        src/widgets/overlays/private/VPopoverPath.cpp
        src/widgets/views/VTreeItemDelegate.cpp
        src/widgets/views/VTreeView.cpp
        src/widgets/views/VFileTreeView.cpp
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

if(VKUI_BUILD_INTERACTION)
    add_library(vkui_buffer STATIC
        include/vkui/Buffer.h
        include/vkui/buffer/BufferStorage.h
        src/buffer/BufferStorage.cpp
    )
    add_library(VkUI::Buffer ALIAS vkui_buffer)
    target_compile_features(vkui_buffer PUBLIC cxx_std_20)
    target_include_directories(vkui_buffer PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    set_target_properties(vkui_buffer PROPERTIES
        EXPORT_NAME Buffer
        OUTPUT_NAME vkui-buffer
        CXX_EXTENSIONS OFF)

    set(_vkui_interaction_sources
        src/vk/private/VkCanonicalKeyEvent.cpp
        src/vk/private/VkCommandRegistry.cpp
        src/vk/private/VkCore.cpp
        src/vk/private/VkCoreBuffer.cpp
        src/vk/private/VkCoreBufferQueries.cpp
        src/vk/private/VkCoreCommand.cpp
        src/vk/private/VkCoreCommandState.cpp
        src/vk/private/VkCoreEditHistory.cpp
        src/vk/private/VkCoreEditing.cpp
        src/vk/private/VkCoreExecution.cpp
        src/vk/private/VkCoreInput.cpp
        src/vk/private/VkCoreInputGrammar.cpp
        src/vk/private/VkCoreInternal.cpp
        src/vk/private/VkCoreInternal.h
        src/vk/private/VkCoreMotion.cpp
        src/vk/private/VkCoreNativeGrammar.h
        src/vk/private/VkCoreNavigation.cpp
        src/vk/private/VkCoreOperators.cpp
        src/vk/private/VkCorePluginManager.cpp
        src/vk/private/VkCoreRegistersMarks.cpp
        src/vk/private/VkCoreRepeat.cpp
        src/vk/private/VkCoreSearchCommands.cpp
        src/vk/private/VkCoreTextObjects.cpp
        src/vk/private/VkCoreVisual.cpp
        src/vk/private/VkCoreWindow.cpp
        src/vk/private/VkCoreWorkspaceState.cpp
        src/vk/private/VkInputEngine.cpp
        src/vk/private/VkUserCommandRegistry.cpp)
    add_library(vkui_interaction STATIC
        include/vkui/Interaction.h
        include/vkui/vk/VkCanonicalKeyEvent.h
        include/vkui/vk/VkCommandRegistry.h
        include/vkui/vk/VkCore.h
        include/vkui/vk/VkCorePluginManager.h
        include/vkui/vk/VkInputEngine.h
        include/vkui/vk/VkTypes.h
        include/vkui/vk/VkUserCommandRegistry.h
        include/vkui/vk/VkWindowCommands.h
        ${_vkui_interaction_sources})
    add_library(VkUI::Interaction ALIAS vkui_interaction)
    target_compile_features(vkui_interaction PUBLIC cxx_std_20)
    target_include_directories(vkui_interaction
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/vkui/vk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/vk/private)
    target_link_libraries(vkui_interaction
        PUBLIC Qt6::Gui ICU::uc ICU::i18n VkUI::Buffer VkUI::Core)
    target_compile_definitions(vkui_interaction PRIVATE
        QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
    set_target_properties(vkui_interaction PROPERTIES
        EXPORT_NAME Interaction
        OUTPUT_NAME vkui-interaction
        AUTOMOC ON
        CXX_EXTENSIONS OFF)

    add_library(vkui_panel STATIC
        include/vkui/Panel.h
        include/vkui/panel/VkPanelLayoutModel.h
        include/vkui/panel/VkSpatialNavigation.h
        src/panel/VkPanelLayoutModel.cpp
        src/panel/VkSpatialNavigation.cpp)
    add_library(VkUI::Panel ALIAS vkui_panel)
    target_compile_features(vkui_panel PUBLIC cxx_std_20)
    target_include_directories(vkui_panel PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(vkui_panel PUBLIC Qt6::Core VkUI::Interaction)
    target_compile_definitions(vkui_panel PRIVATE
        QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
    set_target_properties(vkui_panel PROPERTIES
        EXPORT_NAME Panel
        OUTPUT_NAME vkui-panel
        CXX_EXTENSIONS OFF)

    add_library(vkui_interaction_widgets STATIC
        include/vkui/InteractionWidgets.h
        include/vkui/widgets/vk/VkBlockRegistry.h
        include/vkui/widgets/vk/VkPanelRegistry.h
        include/vkui/widgets/vk/VkVisualBlockPainter.h
        include/vkui/widgets/vk/VkWidgetBlockNavigator.h
        src/widgets/vk/VkBlockRegistry.cpp
        src/widgets/vk/VkPanelRegistry.cpp
        src/widgets/vk/VkVisualBlockPainter.cpp
        src/widgets/vk/VkWidgetBlockNavigator.cpp)
    add_library(VkUI::InteractionWidgets ALIAS vkui_interaction_widgets)
    target_compile_features(vkui_interaction_widgets PUBLIC cxx_std_20)
    target_include_directories(vkui_interaction_widgets PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(vkui_interaction_widgets
        PUBLIC Qt6::Widgets VkUI::Widgets VkUI::Interaction VkUI::Panel)
    target_compile_definitions(vkui_interaction_widgets PRIVATE
        QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
    set_target_properties(vkui_interaction_widgets PROPERTIES
        EXPORT_NAME InteractionWidgets
        OUTPUT_NAME vkui-interaction-widgets
        AUTOMOC ON
        CXX_EXTENSIONS OFF)
endif()

if(VKUI_BUILD_WINDOW)
    if(APPLE)
        enable_language(OBJCXX)
    endif()
    include(cmake/VkUIWindow.cmake)
endif()

if(NOT VKUI_BUILD_SHARED)
    target_compile_definitions(vkui_core PUBLIC VKUI_STATIC)
    target_compile_definitions(vkui_widgets PUBLIC VKUI_STATIC)
    if(TARGET vkui_window)
        target_compile_definitions(vkui_window PUBLIC VKUI_STATIC)
    endif()
endif()

if(VKUI_ENABLE_WARNINGS)
    vkui_enable_compiler_warnings(vkui_core)
    vkui_enable_compiler_warnings(vkui_widgets)
    if(TARGET vkui_buffer)
        vkui_enable_compiler_warnings(vkui_buffer)
        vkui_enable_compiler_warnings(vkui_interaction)
        vkui_enable_compiler_warnings(vkui_panel)
        vkui_enable_compiler_warnings(vkui_interaction_widgets)
    endif()
    if(TARGET vkui_window)
        vkui_enable_compiler_warnings(vkui_window)
    endif()
endif()

if(VKUI_ENABLE_SANITIZERS)
    vkui_enable_sanitizers(vkui_core)
    vkui_enable_sanitizers(vkui_widgets)
    if(TARGET vkui_buffer)
        vkui_enable_sanitizers(vkui_buffer)
        vkui_enable_sanitizers(vkui_interaction)
        vkui_enable_sanitizers(vkui_panel)
        vkui_enable_sanitizers(vkui_interaction_widgets)
    endif()
    if(TARGET vkui_window)
        vkui_enable_sanitizers(vkui_window)
    endif()
endif()

if(VKUI_BUILD_EXAMPLES)
    add_subdirectory(examples/gallery)
endif()
if(VKUI_BUILD_ICON_CHOSEN)
    add_subdirectory(tools/icon-chosen)
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
    if(TARGET vkui_buffer)
        install(TARGETS
                vkui_buffer vkui_interaction vkui_panel
                vkui_interaction_widgets
            EXPORT VkUIInteractionTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
        install(EXPORT VkUIInteractionTargets
            FILE VkUIInteractionTargets.cmake
            NAMESPACE VkUI::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VkUI)
    endif()
    if(TARGET vkui_window)
        install(TARGETS vkui_window
            EXPORT VkUIWindowTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(EXPORT VkUIWindowTargets
            FILE VkUIWindowTargets.cmake
            NAMESPACE VkUI::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VkUI
        )
    endif()
    install(DIRECTORY include/vkui DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "Window.h" EXCLUDE
        PATTERN "window" EXCLUDE
    )
    install(FILES ${VKUI_SYMBOL_HEADER}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkui/core
    )
    if(TARGET vkui_window)
        install(FILES include/vkui/Window.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkui
        )
        install(DIRECTORY include/vkui/window
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkui
        )
    endif()

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
    install(FILES resources/fonts/LICENSE-FIRACODE-NERD-FONT.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/vkui/licenses
        RENAME NerdFontDerivedSVG-LICENSE.txt
    )
    install(DIRECTORY resources/translations/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/vkui/translations
        FILES_MATCHING PATTERN "*.ts"
    )
endif()
