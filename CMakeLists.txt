# One file for both builds: PlatformIO's ESP-IDF firmware build (IDF_PATH set) uses the ESP-IDF project;
# a plain CMake run on the PC builds the portable core and the tests (CMakeLists.pc.txt).
cmake_minimum_required(VERSION 3.16.0)
if(DEFINED ENV{IDF_PATH})
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(axis-bbs)
else()
    include(${CMAKE_CURRENT_LIST_DIR}/CMakeLists.pc.txt)
endif()
