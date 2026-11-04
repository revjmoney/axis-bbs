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
    src/net/telnetstream.cpp
    src/net/crc.cpp
    src/net/xmodem.cpp
    src/net/zmodem.cpp
    src/bbs/node.cpp
    src/bbs/sysop_menu.cpp
    src/bbs/filelib.cpp
    src/bbs/filebrowser.cpp
    src/bbs/zipreader.cpp
    src/bbs/ansiterm.cpp
    src/bbs/archive.cpp
    src/bbs/usersettings.cpp
    src/bbs/ansiviewer.cpp
    src/bbs/rotatelog.cpp
    src/bbs/dosshell.cpp
    src/bbs/dripfont.cpp
    src/bbs/ledsignal.cpp
    src/bbs/msgbase.cpp
    src/bbs/msgreader.cpp
    src/bbs/setupportal.cpp
    src/bbs/splash.cpp
    src/bbs/theme.cpp
    src/bbs/transferjob.cpp
    src/bbs/uploadproc.cpp
    src/bbs/users.cpp
    src/bbs/newuser.cpp
    src/bbs/touchkeyboard.cpp
    src/bbs/wifipicker.cpp
    src/cluster/protocol.cpp
    src/cluster/remotefiles.cpp
    src/cluster/relay.cpp
    src/cluster/doorfs.cpp
    src/cluster/cluster.cpp
    src/bbs/password.cpp
    src/bbs/config.cpp
    src/bbs/setup_wizard.cpp
    src/bbs/setup_gate.cpp
    third_party/sha256/sha256.c
    src/door/cpu.cpp
    src/door/guestmem.cpp
    src/door/mz.cpp
    src/door/dosmem.cpp
    src/door/dos.cpp
    src/door/bios.cpp
    src/door/session.cpp
    src/door/fossil.cpp
    src/door/dropfile.cpp
    src/door/pathresolve.cpp
    src/door/imagekey.cpp
    src/platform/posix/posixhostfs.cpp
    src/platform/pc/consolestream.cpp
    src/platform/pc/tcpstream.cpp
    third_party/i8086/i8086.cpp
    third_party/miniz/miniz_tinfl.c)
target_include_directories(axiscore PUBLIC src third_party/i8086 PRIVATE third_party/sha256 third_party/miniz)

add_executable(axis_tests
    tests/test_main.cpp
    tests/test_telnet.cpp
    tests/test_node.cpp
    tests/test_password.cpp
    tests/test_config.cpp
    tests/test_setup_wizard.cpp
    tests/test_setup_gate.cpp
    tests/test_cpu.cpp
    tests/test_guestmem.cpp
    tests/test_mz.cpp
    tests/test_dos.cpp
    tests/test_bios.cpp
    tests/test_session.cpp
    tests/test_lord_local.cpp
    tests/test_fossil.cpp
    tests/test_dropfile.cpp
    tests/test_lord_telnet.cpp
    tests/test_pathresolve.cpp
    tests/test_imagekey.cpp
    tests/test_telnetstream.cpp
    tests/test_sysop_menu.cpp
    tests/test_files.cpp
    tests/test_zip.cpp
    tests/test_ansiterm.cpp
    tests/test_archive.cpp
    tests/test_usersettings.cpp
    tests/test_oblivion.cpp
    tests/test_multinode.cpp
    tests/test_ansiview.cpp
    tests/test_rotatelog.cpp
    tests/test_dosshell.cpp
    tests/test_ledsignal.cpp
    tests/test_msgbase.cpp
    tests/test_setupportal.cpp
    tests/test_splash.cpp
    tests/test_transfer.cpp
    tests/test_theme.cpp
    tests/test_transferjob.cpp
    tests/test_uploadproc.cpp
    tests/test_palette.cpp
    tests/test_users.cpp
    tests/test_accounts.cpp
    tests/test_wifipicker.cpp
    tests/test_cluster.cpp
    tests/test_remotefiles.cpp
    tests/test_relay.cpp
    tests/test_doorfs.cpp)
target_include_directories(axis_tests PRIVATE third_party/doctest)
target_link_libraries(axis_tests PRIVATE axiscore)
if(WIN32)
  target_link_libraries(axiscore PUBLIC ws2_32)
endif()

add_executable(doorhost tools/doorhost/main.cpp)
target_link_libraries(doorhost PRIVATE axiscore)

target_compile_definitions(axis_tests PRIVATE AXIS_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")

add_executable(xfertest tools/xfertest/main.cpp)
target_link_libraries(xfertest PRIVATE axiscore)
if(WIN32)
  target_link_libraries(xfertest PRIVATE ws2_32)
endif()
