# SPDX-License-Identifier: MIT
# Toolchain for the MPS2-AN385 board (Cortex-M3, no operating system).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
# The board has no runtime to link a test program against, so the compiler is checked by
# compiling rather than by linking.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(JMPXX_BOARD_FLAGS "-mcpu=cortex-m3 -mthumb -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS_INIT "${JMPXX_BOARD_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${JMPXX_BOARD_FLAGS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
