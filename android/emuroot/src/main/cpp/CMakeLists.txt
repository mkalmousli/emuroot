cmake_minimum_required(VERSION 3.22.1)
project(emuroot C)

# The engine is the repository's own C sources, not a copy.
set(EMUROOT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src)
set(EMUROOT_INC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../include)

file(GLOB ENGINE_SOURCES ${EMUROOT_SRC}/*.c)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/main\\.c$")   # the CLI front end is not part of the library

add_library(emuroot SHARED ${ENGINE_SOURCES} emuroot_jni.c)
target_include_directories(emuroot PRIVATE ${EMUROOT_SRC} ${EMUROOT_INC})
target_compile_definitions(emuroot PRIVATE _GNU_SOURCE)
target_compile_options(emuroot PRIVATE -Wall -Wextra -O2 -std=c11)
# Google Play requires 16 KB page-size compatible native libraries (Android 15+).
target_link_options(emuroot PRIVATE -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384)
target_link_libraries(emuroot log)
