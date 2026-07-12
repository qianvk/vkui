# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)
project(VkUIAddSubdirectoryConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(VKUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VKUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(VKUI_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../.." vkui)

add_executable(vkui_add_subdirectory_consumer main.cpp)
target_link_libraries(vkui_add_subdirectory_consumer PRIVATE VkUI::Widgets)

