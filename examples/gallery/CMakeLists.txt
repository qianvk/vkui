# SPDX-License-Identifier: MIT

qt_add_library(vkui_gallery_ui STATIC
    GalleryApplicationController.h
    GalleryApplicationController.cpp
    GalleryContentView.h
    GalleryContentView.cpp
    GalleryPreferencesWindow.h
    GalleryPreferencesWindow.cpp
    GalleryWindow.h
    GalleryWindow.cpp
    pages/StandardWidgetsPage.h
    pages/StandardWidgetsPage.cpp
    pages/SwitchPage.h
    pages/SwitchPage.cpp
    pages/SegmentedControlPage.h
    pages/SegmentedControlPage.cpp
    pages/PopoverPage.h
    pages/PopoverPage.cpp
    pages/ThemePage.h
    pages/ThemePage.cpp
    pages/IconsPage.h
    pages/IconsPage.cpp
    pages/MotionPage.h
    pages/MotionPage.cpp
    pages/LocalizationPage.h
    pages/LocalizationPage.cpp
    pages/WindowPage.h
    pages/WindowPage.cpp
)

target_link_libraries(vkui_gallery_ui PUBLIC VkUI::Widgets VkUI::Window Qt6::Widgets)
target_include_directories(vkui_gallery_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${VKUI_GENERATED_GALLERY_DIR}
)

qt_add_executable(vkui_gallery main.cpp)
target_link_libraries(vkui_gallery PRIVATE vkui_gallery_ui)

# Gallery text is the project's translatable UI. Embedding the catalogs in the
# executable keeps VkUI::Core free of an auxiliary resource target when it is
# exported as a static library.
if(COMMAND qt_add_translations)
    qt_add_translations(vkui_gallery
        SOURCE_TARGETS vkui_gallery_ui
        TS_FILES
            ${PROJECT_SOURCE_DIR}/resources/translations/vkui_en.ts
            ${PROJECT_SOURCE_DIR}/resources/translations/vkui_zh_CN.ts
        RESOURCE_PREFIX "/vkui/translations"
    )
elseif(VKUI_LRELEASE_EXECUTABLE)
    set(_vkui_gallery_qm_dir "${CMAKE_CURRENT_BINARY_DIR}/translations")
    set(_vkui_gallery_qm_files)
    foreach(_vkui_locale IN ITEMS en zh_CN)
        set(_vkui_ts_file
            "${PROJECT_SOURCE_DIR}/resources/translations/vkui_${_vkui_locale}.ts")
        set(_vkui_qm_file "${_vkui_gallery_qm_dir}/vkui_${_vkui_locale}.qm")
        add_custom_command(
            OUTPUT "${_vkui_qm_file}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_vkui_gallery_qm_dir}"
            COMMAND "${VKUI_LRELEASE_EXECUTABLE}" "${_vkui_ts_file}"
                    -qm "${_vkui_qm_file}"
            DEPENDS "${_vkui_ts_file}"
            VERBATIM
        )
        set_source_files_properties("${_vkui_qm_file}" PROPERTIES
            GENERATED TRUE
            QT_RESOURCE_ALIAS "vkui_${_vkui_locale}.qm"
        )
        list(APPEND _vkui_gallery_qm_files "${_vkui_qm_file}")
    endforeach()
    qt_add_resources(vkui_gallery "gallery_translations"
        PREFIX "/vkui/translations"
        FILES ${_vkui_gallery_qm_files}
    )
endif()
set_target_properties(vkui_gallery PROPERTIES
    OUTPUT_NAME vkui-gallery
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "vkui Gallery"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.vkui.gallery"
)

if(VKUI_ENABLE_WARNINGS)
    vkui_enable_compiler_warnings(vkui_gallery_ui)
    vkui_enable_compiler_warnings(vkui_gallery)
endif()
