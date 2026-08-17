# SPDX-License-Identifier: MIT
# The distribution benchmark surface. jmpxx-bench drives one error-propagation kernel,
# implemented identically per mechanism under kernels/, and reports per-call latency
# distributions, the perf gate, and a deterministic callgrind instruction count. The incumbent
# libraries are dev-only dependencies located here; none is on jmpxx's public include surface.
cmake_minimum_required(VERSION 3.20)

# The harness uses the GCC and Clang inline-assembly optimization barrier and the noinline
# attribute, so it is built on those compilers only; the MSVC cell is covered for the library and
# the unwind arm elsewhere and does not build the benchmark surface.
if(MSVC)
  message(STATUS "jmpxx-bench: skipped on MSVC")
  return()
endif()

if(NOT TARGET jmpxx::jmpxx)
  find_package(jmpxx REQUIRED)
endif()

# Incumbents, each optional. A missing one disables its kernel through a JXB_HAVE_* flag and
# the harness reports it unavailable, so the suite builds and runs wherever it lands.
set(JXB_OUTCOME 0)
set(JXB_LEAF 0)
set(JXB_TLEXPECTED 0)
set(JXB_BENCH_INCLUDES)

# Boost.Outcome and Boost.LEAF are header-only; locate their umbrella headers directly rather
# than through a CMake package target, which the system Boost does not always ship for a
# header-only library.
find_path(JXB_BOOST_OUTCOME_DIR boost/outcome.hpp)
find_path(JXB_BOOST_LEAF_DIR boost/leaf.hpp)
if(JXB_BOOST_OUTCOME_DIR)
  set(JXB_OUTCOME 1)
  list(APPEND JXB_BENCH_INCLUDES ${JXB_BOOST_OUTCOME_DIR})
endif()
if(JXB_BOOST_LEAF_DIR)
  set(JXB_LEAF 1)
  list(APPEND JXB_BENCH_INCLUDES ${JXB_BOOST_LEAF_DIR})
endif()

# tl::expected is one header; fetch it non-fatally so an offline build still produces the rest
# of the suite with tl::expected reported unavailable.
set(JXB_TL_DIR ${CMAKE_CURRENT_BINARY_DIR}/tl_include)
set(JXB_TL_HDR ${JXB_TL_DIR}/tl/expected.hpp)
if(NOT EXISTS ${JXB_TL_HDR})
  file(DOWNLOAD
    https://raw.githubusercontent.com/TartanLlama/expected/v1.3.1/include/tl/expected.hpp
    ${JXB_TL_HDR} TIMEOUT 30 STATUS JXB_TL_STATUS)
  # A failed download still leaves a file behind, and an empty header is worse than an
  # absent one: __has_include answers yes, the kernel compiles in, and it names types
  # that are not there. Remove it so the suite reports tl::expected unavailable, which
  # is what the non-fatal fetch is for.
  list(GET JXB_TL_STATUS 0 JXB_TL_CODE)
  if(NOT JXB_TL_CODE EQUAL 0)
    file(REMOVE ${JXB_TL_HDR})
  endif()
endif()
if(EXISTS ${JXB_TL_HDR})
  file(SIZE ${JXB_TL_HDR} JXB_TL_BYTES)
  if(JXB_TL_BYTES GREATER 0)
    set(JXB_TLEXPECTED 1)
    list(APPEND JXB_BENCH_INCLUDES ${JXB_TL_DIR})
  else()
    file(REMOVE ${JXB_TL_HDR})
  endif()
endif()

add_executable(jmpxx-bench
  bench/main.cpp
  kernels/kernel_jmpxx.cpp
  kernels/kernel_jmpxx_slow.cpp
  kernels/kernel_handwritten.cpp
  kernels/kernel_expected.cpp
  kernels/kernel_errorcode.cpp
  kernels/kernel_exceptions.cpp
  kernels/kernel_outcome.cpp
  kernels/kernel_leaf.cpp
  kernels/kernel_tlexpected.cpp
  kernels/boost_support.cpp)
target_link_libraries(jmpxx-bench PRIVATE jmpxx::jmpxx)
target_include_directories(jmpxx-bench PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/kernels
  ${CMAKE_CURRENT_SOURCE_DIR}/../verify  # reporter.hpp, shared with the verification harness
  ${JXB_BENCH_INCLUDES})
target_compile_features(jmpxx-bench PRIVATE cxx_std_23)
target_compile_definitions(jmpxx-bench PRIVATE
  JXB_NOINLINE_FRAMES
  JXB_HAVE_OUTCOME=${JXB_OUTCOME}
  JXB_HAVE_LEAF=${JXB_LEAF}
  JXB_HAVE_TLEXPECTED=${JXB_TLEXPECTED})

# Optimized, the realistic configuration a performance claim must be made in. The suite is the
# strict exception-free configuration, so it builds -fno-exceptions -fno-rtti; the
# exceptions kernel is the one translation unit that needs the tables and RTTI, so it
# overrides those two flags for itself.
if(NOT MSVC)
  target_compile_options(jmpxx-bench PRIVATE -O2)
  target_compile_options(jmpxx-bench PRIVATE -fno-exceptions -fno-rtti)
  set_source_files_properties(kernels/kernel_exceptions.cpp PROPERTIES
    COMPILE_OPTIONS "-fexceptions;-frtti")
endif()

message(STATUS "jmpxx-bench incumbents: outcome=${JXB_OUTCOME} leaf=${JXB_LEAF} tl_expected=${JXB_TLEXPECTED}")

# Bench tests, registered when the parent build enabled testing. The perf gate runs with a
# small epoch budget so it is quick; its inverted run swaps in the slowed kernel and must fail.
if(JMPXX_BENCH_TESTS)
  add_test(NAME bench.run_smoke COMMAND jmpxx-bench run --epochs 8 --epoch-ns 50000)
  set_tests_properties(bench.run_smoke PROPERTIES TIMEOUT 180)

  add_test(NAME bench.gate COMMAND jmpxx-bench gate --epochs 40 --bound 1.6)
  set_tests_properties(bench.gate PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
  add_test(NAME bench.gate.teeth COMMAND jmpxx-bench gate --epochs 40 --bound 1.6 --slow)
  set_tests_properties(bench.gate.teeth PROPERTIES RUN_SERIAL TRUE WILL_FAIL TRUE TIMEOUT 120)

  # The deterministic instruction-count gate, when valgrind is available. The script counts
  # jmpxx and hand-written instructions under callgrind and bounds their ratio; an inverted run
  # inside the script checks that the slowed kernel trips it.
  find_program(JXB_VALGRIND valgrind)
  if(JXB_VALGRIND AND UNIX)
    # The run also records every mechanism the comparison tabulates into a report, and
    # the documented-value gate reads that report rather than a second copy of the
    # numbers. That gate therefore runs after this one, which the fixture declares.
    add_test(NAME bench.callgrind_ir COMMAND bash
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scripts/callgrind_ir_gate.sh
      $<TARGET_FILE:jmpxx-bench> ${JXB_VALGRIND} 200000 110
      ${CMAKE_BINARY_DIR}/callgrind-ir.json)
    set_tests_properties(bench.callgrind_ir PROPERTIES TIMEOUT 300
      FIXTURES_SETUP jmpxx_callgrind_ir)
    find_program(JXB_PYTHON3 python3)
    if(JXB_PYTHON3)
      add_test(NAME doc_claim.bench COMMAND ${JXB_PYTHON3}
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scripts/doc_claim_check.py
        --source-root ${CMAKE_SOURCE_DIR} --build-dir ${CMAKE_BINARY_DIR}
        --values ${CMAKE_SOURCE_DIR}/tests/claims/documented-values.json
        --only-source report
        --report ${CMAKE_BINARY_DIR}/documented-values-bench.json)
      set_tests_properties(doc_claim.bench PROPERTIES
        FIXTURES_REQUIRED jmpxx_callgrind_ir TIMEOUT 120)
    endif()
  endif()
endif()
