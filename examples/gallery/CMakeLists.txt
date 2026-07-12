# SPDX-License-Identifier: MIT

qt_add_executable(vkui_gallery
    main.cpp
    gallery.qrc
    GalleryFonts.h
    GalleryFonts.cpp
    GalleryFileTreeView.h
    GalleryFileTreeView.cpp
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
)

target_link_libraries(vkui_gallery PRIVATE VkUI::Widgets Qt6::Widgets)

# Gallery text is the project's translatable UI. Embedding the catalogs in the
# executable keeps VkUI::Core free of an auxiliary resource target when it is
# exported as a static library.
qt_add_translations(vkui_gallery
    TS_FILES
        ${PROJECT_SOURCE_DIR}/resources/translations/vkui_en.ts
        ${PROJECT_SOURCE_DIR}/resources/translations/vkui_zh_CN.ts
    RESOURCE_PREFIX "/vkui/translations"
)
set_target_properties(vkui_gallery PROPERTIES
    OUTPUT_NAME vkui-gallery
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "vkui Gallery"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.vkui.gallery"
)

if(VKUI_ENABLE_WARNINGS)
    vkui_enable_compiler_warnings(vkui_gallery)
endif()
