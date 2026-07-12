# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)
project(VkUIFindPackageConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets)
find_package(VkUI CONFIG REQUIRED COMPONENTS Core Widgets)

add_executable(vkui_find_package_consumer main.cpp)
target_link_libraries(vkui_find_package_consumer PRIVATE VkUI::Widgets)

