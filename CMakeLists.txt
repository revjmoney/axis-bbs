# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
# PC build: the portable BBS and door code plus its tests. The CYD firmware is built by PlatformIO.
cmake_minimum_required(VERSION 3.20)
project(axis_bbs C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-O2 -Wall -Wextra)

add_library(axiscore STATIC
    src/net/telnet.cpp
    src/bbs/node.cpp
    src/bbs/password.cpp
    src/bbs/config.cpp
    src/bbs/setup_wizard.cpp
    third_party/sha256/sha256.c)
target_include_directories(axiscore PUBLIC src PRIVATE third_party/sha256)

add_executable(axis_tests
    tests/test_main.cpp
    tests/test_telnet.cpp
    tests/test_node.cpp
    tests/test_password.cpp
    tests/test_config.cpp
    tests/test_setup_wizard.cpp)
target_include_directories(axis_tests PRIVATE third_party/doctest)
target_link_libraries(axis_tests PRIVATE axiscore)
