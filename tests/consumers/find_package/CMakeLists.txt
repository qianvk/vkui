# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)
project(VkUIFindPackageConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets)
set(_vkui_components Core Widgets)
if(APPLE OR WIN32)
    list(APPEND _vkui_components Window)
endif()
find_package(VkUI CONFIG REQUIRED COMPONENTS ${_vkui_components})

add_executable(vkui_find_package_consumer main.cpp)
target_link_libraries(vkui_find_package_consumer PRIVATE VkUI::Widgets)
if(TARGET VkUI::Window)
    target_link_libraries(vkui_find_package_consumer PRIVATE VkUI::Window)
    target_compile_definitions(vkui_find_package_consumer PRIVATE VKUI_CONSUMER_HAS_WINDOW=1)
endif()
