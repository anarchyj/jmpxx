# SPDX-License-Identifier: MIT
# storecheck: the embedded field-use program. It is a boot-time image validator for a
# Cortex-M3 board with no operating system, and it boots under system emulation.
#
#   cmake -S reference_app/embedded -B build/storecheck \
#         -DCMAKE_TOOLCHAIN_FILE=reference_app/embedded/board/arm-none-eabi.cmake \
#         -DCMAKE_PREFIX_PATH=<jmpxx install prefix>
#   cmake --build build/storecheck
#   ctest --test-dir build/storecheck
#
# jmpxx comes from an install prefix through find_package, the same channel a consumer
# uses, and littlefs is fetched at configure time and is load-bearing: without it there is
# no image to validate.
cmake_minimum_required(VERSION 3.20)
project(storecheck VERSION 0.1.0 LANGUAGES C CXX)

find_package(jmpxx CONFIG REQUIRED)

include(FetchContent)
FetchContent_Declare(littlefs
  GIT_REPOSITORY https://github.com/littlefs-project/littlefs.git
  GIT_TAG v2.9.3
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(littlefs)

# littlefs ships CMake for a hosted build, so its sources are compiled here with the
# board's flags instead. That is what a firmware integration does, and it keeps the board
# build free of the host-shaped targets that project defines.
add_library(storecheck_lfs STATIC
  ${littlefs_SOURCE_DIR}/lfs.c ${littlefs_SOURCE_DIR}/lfs_util.c)
target_include_directories(storecheck_lfs PUBLIC ${littlefs_SOURCE_DIR})
target_compile_definitions(storecheck_lfs PUBLIC
  LFS_NO_MALLOC LFS_NO_ASSERT LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)
target_compile_options(storecheck_lfs PRIVATE -Wno-unused-parameter)

add_executable(storecheck src/store_check.cpp board/bsp.cpp)
target_link_libraries(storecheck PRIVATE jmpxx::jmpxx storecheck_lfs)
target_compile_options(storecheck PRIVATE -Wall -Wextra)
# The arm needs exception cleanup tables, which are the default, and the linker script
# keeps the .ARM.exidx index they live in on this ABI. Section garbage collection is on to
# show the tables survive it.
target_link_options(storecheck PRIVATE
  -T ${CMAKE_CURRENT_SOURCE_DIR}/board/mps2_an385.ld
  -nostartfiles -Wl,--gc-sections --specs=nano.specs)
set_target_properties(storecheck PROPERTIES SUFFIX ".elf")

find_program(STORECHECK_QEMU qemu-system-arm)
if(STORECHECK_QEMU)
  enable_testing()
  # The board reports through semihosting, and the emulator does not carry the program's
  # exit code, so the printed verdict is what the check reads.
  add_test(NAME storecheck.board COMMAND ${CMAKE_COMMAND}
    -DQEMU=${STORECHECK_QEMU} -DIMAGE=$<TARGET_FILE:storecheck>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/board/run_board.cmake)
endif()
