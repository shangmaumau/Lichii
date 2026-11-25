# SPDX-FileCopyrightText: Copyright 2026 Lichii Project
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(keyboard_tests LANGUAGES CXX)
    enable_testing()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets Test)

add_executable(keyboard_tests
    keyboard.cpp
    ../../lichii/util/keyboard_controller.cpp
    ../../lichii/util/keyboard_input.cpp
    ../../lichii/util/keyboard_overlay.cpp
    ../../lichii/util/keyboard_panel.cpp
    ../../lichii/util/keyboard_panel.h
    ../../lichii/keyboard.qrc
)
target_compile_features(keyboard_tests PRIVATE cxx_std_20)
target_compile_definitions(keyboard_tests PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_include_directories(keyboard_tests PRIVATE ../..)
target_link_libraries(keyboard_tests PRIVATE Qt6::Widgets Qt6::Test)
if(MSVC)
    target_compile_options(keyboard_tests PRIVATE /utf-8)
endif()

add_test(NAME keyboard COMMAND keyboard_tests)
set_tests_properties(keyboard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
