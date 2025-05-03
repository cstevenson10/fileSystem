# //////////////////////////////////////////////////////////////////////////
# ///
# /// Copyright (c) 2020 Prof. AJ Bieszczad. All rights reserved.
# ///
# //////////////////////////////////////////////////////////////////////////

cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project(cifs VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall --pedantic -g -O0 -D_DEBUG -DNO_FUSE_DEBUG")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" ${CMAKE_MODULE_PATH})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(CMAKE_BUILD_TYPE Debug)

#
#  The following comes from:
#  https://gitlab.kitware.com/cmake/community/-/wikis/doc/tutorials/How-To-Write-Platform-Checks
#
if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

# if(NOT LINUX) should work, too, if you need that
if(LINUX)
    message(STATUS ">>> Building with FUSE")
    find_package(FUSE REQUIRED)
    include_directories(${FUSE_INCLUDE_DIR})
else()
    message(STATUS ">>> Building without FUSE")
    # stuff that should happen not on Linux
endif()

include_directories(inc)

add_executable(cifs src/test_cifs.c src/cifs.c)
target_link_libraries(cifs ${FUSE_LIBRARIES})

add_executable(blockVolume src/blockVolume.c)
