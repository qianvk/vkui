# SPDX-License-Identifier: MIT

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(vkui_add_test test_name source_file)
    qt_add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE VkUI::Widgets Qt6::Test)
    target_include_directories(${test_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/core/icons/private
        ${PROJECT_SOURCE_DIR}/src/widgets/overlays/private
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30
    )
    if(VKUI_ENABLE_WARNINGS)
        vkui_enable_compiler_warnings(${test_name})
    endif()
endfunction()

vkui_add_test(tst_theme tst_theme.cpp)
vkui_add_test(tst_theme_lifecycle tst_theme_lifecycle.cpp)
vkui_add_test(tst_icons tst_icons.cpp)
vkui_add_test(tst_motion tst_motion.cpp)
vkui_add_test(tst_disclosuretree tst_disclosuretree.cpp)
vkui_add_test(tst_switch tst_switch.cpp)
vkui_add_test(tst_segmentedcontrol tst_segmentedcontrol.cpp)
vkui_add_test(tst_popoverplacement tst_popoverplacement.cpp)
vkui_add_test(tst_popoverpath tst_popoverpath.cpp)
vkui_add_test(tst_popoverinteraction tst_popoverinteraction.cpp)
vkui_add_test(tst_style tst_style.cpp)

if(VKUI_BUILD_INTERACTION)
    add_executable(tst_buffer tst_buffer.cpp)
    target_link_libraries(tst_buffer PRIVATE VkUI::Buffer)
    add_test(NAME tst_buffer COMMAND tst_buffer)
    set_tests_properties(tst_buffer PROPERTIES TIMEOUT 20)

    function(vkui_add_interaction_test test_name source_file)
        qt_add_executable(${test_name} ${source_file})
        target_link_libraries(${test_name} PRIVATE
            VkUI::Interaction Qt6::Test)
        add_test(NAME ${test_name} COMMAND ${test_name})
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 30)
        if(VKUI_ENABLE_WARNINGS)
            vkui_enable_compiler_warnings(${test_name})
        endif()
    endfunction()

    vkui_add_interaction_test(tst_interaction tst_interaction.cpp)
    vkui_add_interaction_test(
        tst_interaction_plugins tst_interaction_plugins.cpp)
    vkui_add_interaction_test(
        tst_interaction_hints tst_interaction_hints.cpp)
    vkui_add_interaction_test(
        tst_interaction_editing tst_interaction_editing.cpp)
    vkui_add_interaction_test(
        tst_interaction_dispatch tst_interaction_dispatch.cpp)
    vkui_add_interaction_test(
        tst_interaction_user_commands tst_interaction_user_commands.cpp)
    vkui_add_interaction_test(tst_panel tst_panel.cpp)
    target_link_libraries(tst_panel PRIVATE VkUI::Panel)
    vkui_add_test(tst_interaction_widgets tst_interaction_widgets.cpp)
    target_link_libraries(tst_interaction_widgets PRIVATE
        VkUI::InteractionWidgets VkUI::Panel)
endif()

if(TARGET VkUI::Window)
    vkui_add_test(tst_window tst_window.cpp)
    target_link_libraries(tst_window PRIVATE VkUI::Window)
endif()

# Windows shared libraries export only the public ABI. Pure private helpers are
# compiled into the tests that exercise them so testability never makes those
# implementation symbols part of the installed DLL contract.
if(WIN32 AND VKUI_BUILD_SHARED)
    target_sources(tst_icons PRIVATE
        ${PROJECT_SOURCE_DIR}/src/core/icons/private/VkIconCache.cpp
    )
    target_sources(tst_popoverplacement PRIVATE
        ${PROJECT_SOURCE_DIR}/src/widgets/overlays/private/VkPopoverPlacementEngine.cpp
    )
    target_sources(tst_popoverpath PRIVATE
        ${PROJECT_SOURCE_DIR}/src/widgets/overlays/private/VkPopoverPath.cpp
        ${PROJECT_SOURCE_DIR}/src/widgets/overlays/private/VkPopoverShadowCache.cpp
    )
    target_sources(tst_style PRIVATE
        ${PROJECT_SOURCE_DIR}/src/widgets/style/private/VkStylePainter.cpp
        ${PROJECT_SOURCE_DIR}/src/widgets/animation/private/VkWidgetAnimation.cpp
    )
endif()
