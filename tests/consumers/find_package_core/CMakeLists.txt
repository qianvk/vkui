# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)
project(VkUICoreOnlyConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(VkUI CONFIG REQUIRED COMPONENTS Core)

add_executable(vkui_core_only_consumer main.cpp)
target_link_libraries(vkui_core_only_consumer PRIVATE VkUI::Core)

