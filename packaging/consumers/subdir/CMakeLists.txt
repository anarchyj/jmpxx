# SPDX-License-Identifier: MIT
# A consumer that pulls jmpxx in as a subproject. FetchContent_MakeAvailable runs this
# same add_subdirectory path once the content is fetched, so building this checks the
# FetchContent channel as well. JMPXX_ROOT points at the jmpxx checkout.
cmake_minimum_required(VERSION 3.20)
project(jmpxx_subdir_consumer CXX)

if(NOT DEFINED JMPXX_ROOT)
  message(FATAL_ERROR "set -DJMPXX_ROOT=<path to the jmpxx checkout>")
endif()

# A real consumer would write:
#   include(FetchContent)
#   FetchContent_Declare(jmpxx GIT_REPOSITORY https://github.com/anarchyj/jmpxx.git
#                        GIT_TAG v0.1.4 FIND_PACKAGE_ARGS NAMES jmpxx)
#   FetchContent_MakeAvailable(jmpxx)
# which resolves to the add_subdirectory below. The local path keeps the channel test
# offline and deterministic.
add_subdirectory(${JMPXX_ROOT} ${CMAKE_BINARY_DIR}/jmpxx-src)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE jmpxx::jmpxx)

enable_testing()
add_test(NAME consumer COMMAND consumer)
