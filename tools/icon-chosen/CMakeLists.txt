# SPDX-License-Identifier: MIT

qt_add_executable(vkui_icon_chosen
    app/main.cpp
    app/IconCatalogDelegate.h
    app/IconCatalogDelegate.cpp
    app/IconCatalogModel.h
    app/IconCatalogModel.cpp
    app/IconCatalogProxyModel.h
    app/IconCatalogProxyModel.cpp
    app/IconChosenWindow.h
    app/IconChosenWindow.cpp
    app/IconSelectionStore.h
    app/IconSelectionStore.cpp
    app/NerdSymbolIcon.h
    app/NerdSymbolIcon.cpp
)

file(GLOB _icon_chosen_catalog CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/catalog/*.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/catalog/catalog.tsv"
)
qt_add_resources(vkui_icon_chosen "icon_chosen_catalog"
    PREFIX "/icon-chosen/catalog"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/catalog"
    BIG_RESOURCES
    FILES ${_icon_chosen_catalog}
)

target_link_libraries(vkui_icon_chosen PRIVATE VkUI::Widgets Qt6::Widgets Qt6::Svg)
set_target_properties(vkui_icon_chosen PROPERTIES
    OUTPUT_NAME icon-chosen
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "VkUI Icon Chooser"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.vkui.icon-chosen"
)

if(VKUI_ENABLE_WARNINGS)
    vkui_enable_compiler_warnings(vkui_icon_chosen)
endif()
