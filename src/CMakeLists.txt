# The DOS runtime is dosemu/. It is not part of this image. File locks stay in src/door/locktable.cpp.
file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.c)
list(FILTER app_sources EXCLUDE REGEX ".*/platform/pc/.*")
idf_component_register(SRCS ${app_sources} INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -w -O2 -std=gnu++17)
