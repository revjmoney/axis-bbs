# The DOS runtime is dosemu/. It is not part of this image. File locks stay in src/door/locktable.cpp.
file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.c)
list(FILTER app_sources EXCLUDE REGEX ".*/platform/pc/.*")
list(FILTER app_sources EXCLUDE REGEX ".*/platform/linux/.*")  # the native Linux program's host layer
idf_component_register(SRCS ${app_sources} INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -w -O2 -std=gnu++17)
# Sysop Net's portable code is built for size, not speed: it waits on the network, and -O2 makes it about 1.6 times
# bigger. Spelled --optimize=s because platformio.ini's build_unflags strips every -Os.
set(net_size_sources areas.cpp outlog.cpp record.cpp revkeys.cpp session.cpp sign.cpp state.cpp)
list(TRANSFORM net_size_sources PREPEND ${CMAKE_SOURCE_DIR}/src/net/)
set_source_files_properties(${net_size_sources} PROPERTIES COMPILE_OPTIONS --optimize=s)
