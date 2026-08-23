# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)
project(nerd_symbol_generator LANGUAGES CXX)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

qt_add_executable(nerd_symbol_generator main.cpp)
target_compile_features(nerd_symbol_generator PRIVATE cxx_std_20)
target_link_libraries(nerd_symbol_generator PRIVATE Qt6::Core Qt6::Gui)
