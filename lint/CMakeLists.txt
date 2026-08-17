# SPDX-License-Identifier: MIT
# The out-of-tree companion check set. jmpxx-lint is built against the installed LLVM and Clang
# development libraries; it is a developer tool, never a runtime or include dependency of the
# library. The whole directory is skipped when those libraries are absent, so the rest
# of the build still completes.
cmake_minimum_required(VERSION 3.20)

# Pin to the LLVM toolset whose development package is complete in this environment, while
# respecting an explicit -DLLVM_DIR override. Ubuntu installs the CMake config under
# /usr/lib/llvm-<n>/lib/cmake.
if(NOT LLVM_DIR AND EXISTS /usr/lib/llvm-18/lib/cmake/llvm)
  set(LLVM_DIR /usr/lib/llvm-18/lib/cmake/llvm)
endif()
if(NOT Clang_DIR AND EXISTS /usr/lib/llvm-18/lib/cmake/clang)
  set(Clang_DIR /usr/lib/llvm-18/lib/cmake/clang)
endif()

find_package(LLVM CONFIG QUIET)
find_package(Clang CONFIG QUIET)
if(NOT LLVM_FOUND OR NOT Clang_FOUND)
  message(STATUS "jmpxx-lint: LLVM/Clang development packages not found; the lint companion is skipped")
  return()
endif()
message(STATUS "jmpxx-lint: using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(AddLLVM)
add_definitions(${LLVM_DEFINITIONS})

add_executable(jmpxx-lint jmpxx_lint.cpp)
target_include_directories(jmpxx-lint SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
# The LLVM public headers require C++17 and are built without RTTI; match both so the tool links.
target_compile_features(jmpxx-lint PRIVATE cxx_std_17)
if(DEFINED LLVM_ENABLE_RTTI AND NOT LLVM_ENABLE_RTTI)
  target_compile_options(jmpxx-lint PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-rtti>)
endif()

# The apt LLVM packages ship the everything-libraries libclang-cpp and libLLVM, which is the
# least-fragile way to link a libTooling program; fall back to the component archives otherwise.
if(TARGET clang-cpp AND TARGET LLVM)
  target_link_libraries(jmpxx-lint PRIVATE clang-cpp LLVM)
else()
  target_link_libraries(jmpxx-lint PRIVATE
    clangTooling clangToolingCore clangASTMatchers clangFrontend clangSerialization
    clangParse clangSema clangAnalysis clangEdit clangRewrite clangDriver
    clangAST clangLex clangBasic)
  llvm_map_components_to_libnames(JXB_LLVM_LIBS support core option)
  target_link_libraries(jmpxx-lint PRIVATE ${JXB_LLVM_LIBS})
endif()

# Lint tests, registered when the parent build enabled testing. Each runs the tool over a
# fixture and asserts the exact finding set: every violation is flagged and the clean file,
# including its tricky non-violations, produces none.
if(JMPXX_LINT_TESTS)
  set(LF ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
  add_test(NAME lint.violations COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scripts/lint_check.sh
    $<TARGET_FILE:jmpxx-lint> ${CMAKE_CURRENT_SOURCE_DIR}/.. expect-findings ${LF}/violations.cpp)
  add_test(NAME lint.clean COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scripts/lint_check.sh
    $<TARGET_FILE:jmpxx-lint> ${CMAKE_CURRENT_SOURCE_DIR}/.. expect-clean ${LF}/clean.cpp)
endif()
