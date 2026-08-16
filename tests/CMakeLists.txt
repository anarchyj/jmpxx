# SPDX-License-Identifier: MIT
# Test tiers, each independently runnable through CTest.

set(JMPXX_TEST_WARNINGS)
if(NOT MSVC)
  set(JMPXX_TEST_WARNINGS -Wall -Wextra)
endif()

# Toolchains some tiers below need are located once, up front, so that every tier
# that depends on them sees the result regardless of where it appears in this file.
find_program(JMPXX_GXX13 g++-13)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(JMPXX_CLANGXX ${CMAKE_CXX_COMPILER} CACHE FILEPATH
    "Clang++ used by Clang-only verification tiers" FORCE)
else()
  find_program(JMPXX_CLANGXX NAMES clang++ clang++-19 clang++-18 clang++-17 clang++-16)
endif()
find_program(JMPXX_AFL_CLANG_FASTXX afl-clang-fast++)
find_program(JMPXX_SCAN_BUILD NAMES scan-build scan-build-19 scan-build-18 scan-build-17 scan-build-16)
set(JMPXX_LLVM_TOOL_SUFFIX)
if(JMPXX_CLANGXX)
  get_filename_component(JMPXX_CLANGXX_NAME ${JMPXX_CLANGXX} NAME)
  if(JMPXX_CLANGXX_NAME MATCHES "clang\\+\\+-([0-9]+)$")
    set(JMPXX_LLVM_TOOL_SUFFIX ${CMAKE_MATCH_1})
  elseif(JMPXX_CLANGXX MATCHES "llvm-([0-9]+)/bin/clang\\+\\+$")
    set(JMPXX_LLVM_TOOL_SUFFIX ${CMAKE_MATCH_1})
  endif()
endif()
set(JMPXX_LLVM_PROFDATA_NAMES)
set(JMPXX_LLVM_COV_NAMES)
if(JMPXX_LLVM_TOOL_SUFFIX)
  list(APPEND JMPXX_LLVM_PROFDATA_NAMES llvm-profdata-${JMPXX_LLVM_TOOL_SUFFIX})
  list(APPEND JMPXX_LLVM_COV_NAMES llvm-cov-${JMPXX_LLVM_TOOL_SUFFIX})
endif()
list(APPEND JMPXX_LLVM_PROFDATA_NAMES llvm-profdata llvm-profdata-19 llvm-profdata-18 llvm-profdata-17 llvm-profdata-16)
list(APPEND JMPXX_LLVM_COV_NAMES llvm-cov llvm-cov-19 llvm-cov-18 llvm-cov-17 llvm-cov-16)
list(REMOVE_DUPLICATES JMPXX_LLVM_PROFDATA_NAMES)
list(REMOVE_DUPLICATES JMPXX_LLVM_COV_NAMES)
find_program(JMPXX_LLVM_PROFDATA NAMES ${JMPXX_LLVM_PROFDATA_NAMES})
find_program(JMPXX_LLVM_COV NAMES ${JMPXX_LLVM_COV_NAMES})
find_program(JMPXX_PYTHON3 python3)
set(JMPXX_MSAN_LIBCXX_ROOT "" CACHE PATH
  "Root of an MSan-instrumented libc++ install used by the memory-sanitizer gate")

# Compile-and-run tiers: static and constexpr assert at translation time (a
# failed assertion fails the build), the property tier exercises randomized
# sequences at run time.
add_executable(jmpxx_static static/static_asserts.cpp)
add_executable(jmpxx_constexpr constexpr/constexpr_tests.cpp)
add_executable(jmpxx_property property/property_tests.cpp)
foreach(t jmpxx_static jmpxx_constexpr jmpxx_property)
  target_link_libraries(${t} PRIVATE jmpxx::jmpxx)
  target_compile_options(${t} PRIVATE ${JMPXX_TEST_WARNINGS})
endforeach()
add_test(NAME static COMMAND jmpxx_static)
add_test(NAME constexpr COMMAND jmpxx_constexpr)
add_test(NAME property COMMAND jmpxx_property)

# Adversarial portable-surface hardening. These are gates, not examples: each
# defect class has an inverted self-test so acceptance can trust the gate.
add_executable(jmpxx_adversarial_structured adversarial/structured_sequences.cpp)
add_executable(jmpxx_adversarial_structured_teeth adversarial/structured_sequences.cpp)
add_executable(jmpxx_differential_oracle adversarial/differential_oracle.cpp)
add_executable(jmpxx_exception_safety adversarial/exception_safety.cpp)
add_executable(jmpxx_model_lifecycle adversarial/model_lifecycle.cpp)
add_executable(jmpxx_usage_scenarios adversarial/usage_scenarios.cpp)
foreach(t jmpxx_adversarial_structured jmpxx_adversarial_structured_teeth
          jmpxx_differential_oracle jmpxx_exception_safety jmpxx_model_lifecycle
          jmpxx_usage_scenarios)
  target_link_libraries(${t} PRIVATE jmpxx::jmpxx)
  target_compile_features(${t} PRIVATE cxx_std_23)
  target_compile_options(${t} PRIVATE ${JMPXX_TEST_WARNINGS})
endforeach()
target_compile_definitions(jmpxx_adversarial_structured_teeth PRIVATE
  JMPXX_ADVERSARIAL_INJECT_DEFECT=1 JMPXX_DIAGNOSTICS_ENABLED=1)
target_compile_definitions(jmpxx_adversarial_structured PRIVATE
  JMPXX_DIAGNOSTICS_ENABLED=1)
target_compile_definitions(jmpxx_model_lifecycle PRIVATE
  JMPXX_DIAGNOSTICS_ENABLED=1
  JMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE)
target_compile_definitions(jmpxx_usage_scenarios PRIVATE
  JMPXX_DIAGNOSTICS_ENABLED=0
  JMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE)
if(NOT MSVC)
  target_compile_options(jmpxx_usage_scenarios PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME adversarial.fuzz.structured COMMAND jmpxx_adversarial_structured)
add_test(NAME adversarial.fuzz.structured.teeth
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/scripts/expect_command_failure.sh
  $<TARGET_FILE:jmpxx_adversarial_structured_teeth> --inject-defect)
set_tests_properties(adversarial.fuzz.structured.teeth PROPERTIES WILL_FAIL TRUE)

add_test(NAME adversarial.differential COMMAND jmpxx_differential_oracle)
add_test(NAME adversarial.differential.teeth
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/scripts/expect_command_failure.sh
  $<TARGET_FILE:jmpxx_differential_oracle> --inject-divergence)
set_tests_properties(adversarial.differential.teeth PROPERTIES WILL_FAIL TRUE)

add_test(NAME adversarial.exception_safety COMMAND jmpxx_exception_safety)
add_test(NAME adversarial.exception_safety.teeth
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/scripts/expect_command_failure.sh
  $<TARGET_FILE:jmpxx_exception_safety> --inject-valueless)
set_tests_properties(adversarial.exception_safety.teeth PROPERTIES WILL_FAIL TRUE)

add_test(NAME model.lifecycle COMMAND jmpxx_model_lifecycle)
add_test(NAME model.lifecycle.teeth
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/scripts/expect_command_failure.sh
  $<TARGET_FILE:jmpxx_model_lifecycle> --inject-wrong-transition)
set_tests_properties(model.lifecycle.teeth PROPERTIES WILL_FAIL TRUE)

add_test(NAME adversarial.usage_scenarios COMMAND jmpxx_usage_scenarios)

# Monadic-ops tier: and_then/or_else/transform/transform_error match std::expected,
# exercised over the three policies and in constant evaluation. Built in the strict
# configuration (exceptions and RTTI off) on the compilers whose flags spell it.
add_executable(jmpxx_monadic monadic/monadic_tests.cpp)
target_link_libraries(jmpxx_monadic PRIVATE jmpxx::jmpxx)
target_compile_options(jmpxx_monadic PRIVATE ${JMPXX_TEST_WARNINGS})
if(NOT MSVC)
  target_compile_options(jmpxx_monadic PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME monadic COMMAND jmpxx_monadic)

# Harness probes are observable capabilities run directly as tests.
foreach(probe size alloc destructors semantics levels policies diagnostics interop reflect platform)
  add_test(NAME probe.${probe} COMMAND jmpxx-verify ${probe})
endforeach()

# The no-alloc gate's inverted self-test: an injected heap allocation on the measured
# path must be observed by the hooked allocator and fail the probe, so the gate catches
# an allocating path rather than passing because nothing exercises it.
add_test(NAME probe.alloc.teeth COMMAND jmpxx-verify alloc --inject-alloc)
set_tests_properties(probe.alloc.teeth PROPERTIES WILL_FAIL TRUE)

# Reflection forward layer, hand-written fallback path. Building the harness and this
# tier on a C++20 toolchain exercises the fallback that derives enum names, the failure
# set, and the enum-keyed error domain by parsing the compiler signature; the reflection
# path is exercised on the reflection-capable continuous-integration cell, which runs the
# same fixture and must produce the same results. The fixture self-checks its expected
# metadata and exits non-zero on a mismatch.
add_executable(jmpxx_reflect_fallback reflect/reflect_behavior.cpp)
target_link_libraries(jmpxx_reflect_fallback PRIVATE jmpxx::jmpxx)
target_compile_options(jmpxx_reflect_fallback PRIVATE ${JMPXX_TEST_WARNINGS})
add_test(NAME reflect.fallback COMMAND jmpxx_reflect_fallback)

# Dual-personality model: the rich and type-erased policies and the debug-only
# diagnostic layer, each tier independently runnable.

# Behavioral tier, diagnostics on: origin, per-hop causal chain, landing lifetime,
# static-initialization safety, the type-erased boundary, and lossless decay.
add_executable(jmpxx_policy_behavior policy/behavior.cpp)
target_link_libraries(jmpxx_policy_behavior PRIVATE jmpxx::jmpxx)
target_compile_definitions(jmpxx_policy_behavior PRIVATE JMPXX_DIAGNOSTICS_ENABLED=1)
target_compile_options(jmpxx_policy_behavior PRIVATE ${JMPXX_TEST_WARNINGS})
add_test(NAME policy.behavior COMMAND jmpxx_policy_behavior)

# Static tier, both personalities (building is the test): every policy keeps the
# transport trivially copyable, the debug rich error carries the handle, and the
# release rich error equals the minimal error in layout.
add_executable(jmpxx_policy_parity_debug policy/parity.cpp)
target_link_libraries(jmpxx_policy_parity_debug PRIVATE jmpxx::jmpxx)
target_compile_definitions(jmpxx_policy_parity_debug PRIVATE JMPXX_DIAGNOSTICS_ENABLED=1)
add_test(NAME policy.parity_debug COMMAND jmpxx_policy_parity_debug)

add_executable(jmpxx_policy_parity_release policy/parity.cpp)
target_link_libraries(jmpxx_policy_parity_release PRIVATE jmpxx::jmpxx)
target_compile_definitions(jmpxx_policy_parity_release PRIVATE JMPXX_DIAGNOSTICS_ENABLED=0)
add_test(NAME policy.parity_release COMMAND jmpxx_policy_parity_release)

# No-RTTI cell: the type-erased policy's virtual dispatch and the rich policy build
# and run with RTTI disabled. GCC/Clang flag spellings, so it runs on those.
if(NOT MSVC)
  add_executable(jmpxx_policy_erased_nortti policy/erased_nortti.cpp)
  target_link_libraries(jmpxx_policy_erased_nortti PRIVATE jmpxx::jmpxx)
  target_compile_definitions(jmpxx_policy_erased_nortti PRIVATE JMPXX_DIAGNOSTICS_ENABLED=1)
  target_compile_options(jmpxx_policy_erased_nortti PRIVATE -fno-rtti -fno-exceptions ${JMPXX_TEST_WARNINGS})
  add_test(NAME policy.erased_nortti COMMAND jmpxx_policy_erased_nortti)
endif()

# Sanitizer tiers for the out-of-band store: the thread sanitizer over concurrent
# rich failures and the address/undefined sanitizers over landing-scope exit.
if(UNIX AND JMPXX_CLANGXX)
  add_test(NAME policy.sanitizer_thread COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/sanitizer_diag_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    thread ${CMAKE_SOURCE_DIR}/tests/policy/concurrent.cpp)
  add_test(NAME policy.sanitizer_address COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/sanitizer_diag_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    address,undefined ${CMAKE_SOURCE_DIR}/tests/policy/behavior.cpp)
endif()

# Include-boundary scan: the minimal core pulls no rich, erased, diagnostic, or
# source-location header.
if(JMPXX_GXX13 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME include_boundary COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/include_boundary_check.sh
    ${JMPXX_GXX13} ${CMAKE_SOURCE_DIR}/include)
endif()

# Platform-fence scan: every platform-specific or ABI-specific construct lives
# inside platform/ or the fenced unwind arm, so a raw target macro outside them is
# rejected. The inverted case scans a known-bad header that leaks a platform macro
# outside the fence and must fail. Pure text scan, so it runs anywhere bash does.
if(UNIX)
  add_test(NAME platform_fence COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/platform_fence_check.sh
    ${CMAKE_SOURCE_DIR}/include/jmpxx)
  add_test(NAME platform_fence.teeth COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/platform_fence_check.sh
    ${CMAKE_SOURCE_DIR}/tests/fence/known_bad)
  set_tests_properties(platform_fence.teeth PROPERTIES WILL_FAIL TRUE)
endif()

# Interop bridges: round-trip fidelity across the std::expected, std::error_code,
# exception, and optional-like adapters. Built at C++23, the standard that provides
# std::expected, with exceptions enabled so the exception bridge is present. The
# bridges are extensions, so requiring C++23 here does not bear on the core's C++20
# support, which the C++20 cell verifies separately.
add_executable(jmpxx_interop_roundtrip interop/roundtrip.cpp)
target_link_libraries(jmpxx_interop_roundtrip PRIVATE jmpxx::jmpxx)
target_compile_features(jmpxx_interop_roundtrip PRIVATE cxx_std_23)
target_compile_options(jmpxx_interop_roundtrip PRIVATE ${JMPXX_TEST_WARNINGS})
add_test(NAME interop.roundtrip COMMAND jmpxx_interop_roundtrip)

# Interop under -fno-exceptions -fno-rtti: the expected, error_code, and adapter
# bridges work and the exception bridge is absent. GCC/Clang flag spellings, so this
# cell runs on those compilers.
if(NOT MSVC)
  add_executable(jmpxx_interop_noexc interop/no_exceptions_cell.cpp)
  target_link_libraries(jmpxx_interop_noexc PRIVATE jmpxx::jmpxx)
  target_compile_features(jmpxx_interop_noexc PRIVATE cxx_std_23)
  target_compile_options(jmpxx_interop_noexc PRIVATE
    -fno-exceptions -fno-rtti ${JMPXX_TEST_WARNINGS})
  add_test(NAME interop.no_exceptions COMMAND jmpxx_interop_noexc)
endif()

# The public headers survive a consumer whose own headers came first. The Trusted Firmware
# and OP-TEE convention defines U, UL and ULL as integer-suffix macros, which collided with
# a type alias the transport used in a call-shaped position; a firmware consumer could not
# include jmpxx after their own headers. Compiling this fixture is the test.
add_executable(jmpxx_firmware_macros interop/firmware_macros.cpp)
target_link_libraries(jmpxx_firmware_macros PRIVATE jmpxx::jmpxx)
target_compile_options(jmpxx_firmware_macros PRIVATE ${JMPXX_TEST_WARNINGS})
add_test(NAME interop.firmware_macros COMMAND jmpxx_firmware_macros)

# The std::expected bridge preserves the freestanding property: it builds and runs
# -ffreestanding and the minimal core pulls no <expected> or <system_error>. Pinned
# to g++-13 on Linux, the one libstdc++ that honors -ffreestanding.
if(JMPXX_GXX13 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME interop.freestanding_expected COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/interop_freestanding_check.sh
    ${JMPXX_GXX13} ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/interop/freestanding_expected.cpp
    ${CMAKE_BINARY_DIR}/interop_fs_expected)
endif()

# Boundary-input validation. The behavioral tier
# feeds malformed serialized input across the decode boundary under the address and
# undefined-behavior sanitizers, so an out-of-bounds read is a finding rather than a
# silent corruption. The fuzz tier drives the boundary and the bridges with random
# input under libFuzzer, bounded for continuous integration. Both are Clang tiers.
if(UNIX AND JMPXX_CLANGXX)
  add_test(NAME interop.boundary_sanitizer COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/sanitizer_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/interop/boundary.cpp)
endif()
# The fuzz tier needs libFuzzer, which ships with Clang's compiler-rt on Linux but
# not with the Apple toolchain, so it is restricted to Linux rather than every UNIX.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND JMPXX_CLANGXX)
  add_test(NAME interop.fuzz COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/fuzz_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/fuzz/fuzz_interop.cpp 15)
  set_tests_properties(interop.fuzz PROPERTIES TIMEOUT 120)
endif()

# Multi-engine fuzzing over the full portable surface. The libFuzzer and AFL
# runs drive the same semantics with different fuzzing engines, and each has a
# known-bad injected build that must fail.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND JMPXX_CLANGXX)
  add_test(NAME adversarial.fuzz.libfuzzer COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/portable_fuzz_libfuzzer_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/adversarial/portable_libfuzzer.cpp 10 clean)
  add_test(NAME adversarial.fuzz.libfuzzer.teeth COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/portable_fuzz_libfuzzer_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/adversarial/portable_libfuzzer.cpp 2 inject)
  set_tests_properties(adversarial.fuzz.libfuzzer PROPERTIES TIMEOUT 90)
  set_tests_properties(adversarial.fuzz.libfuzzer.teeth PROPERTIES WILL_FAIL TRUE TIMEOUT 60)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND JMPXX_AFL_CLANG_FASTXX)
  add_test(NAME adversarial.fuzz.afl COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/portable_fuzz_afl_check.sh
    ${JMPXX_AFL_CLANG_FASTXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/adversarial/portable_afl.cpp 6 clean)
  add_test(NAME adversarial.fuzz.afl.teeth COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/portable_fuzz_afl_check.sh
    ${JMPXX_AFL_CLANG_FASTXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/adversarial/portable_afl.cpp 2 inject)
  set_tests_properties(adversarial.fuzz.afl PROPERTIES TIMEOUT 90)
  set_tests_properties(adversarial.fuzz.afl.teeth PROPERTIES WILL_FAIL TRUE TIMEOUT 60)
endif()

# Graded hardening and configuration matrix. The hardening gate checks live
# fail-fast behavior and generated-code absence; the configuration matrix runs the
# meaningful switch combinations, including stack-trace capture under sanitizers.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND JMPXX_CLANGXX
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
  add_test(NAME hardening.mode COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/hardening_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/hardening/wrong_state_access.cpp
    ${CMAKE_SOURCE_DIR}/tests/hardening/erased_null_domain.cpp clean)
  add_test(NAME hardening.mode.teeth COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/hardening_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/hardening/wrong_state_access.cpp
    ${CMAKE_SOURCE_DIR}/tests/hardening/erased_null_domain.cpp inject)
  set_tests_properties(hardening.mode.teeth PROPERTIES WILL_FAIL TRUE)

  add_test(NAME config_matrix COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/config_matrix_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/adversarial/config_matrix.cpp clean)
  add_test(NAME config_matrix.teeth COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/config_matrix_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/adversarial/config_matrix.cpp inject)
  set_tests_properties(config_matrix.teeth PROPERTIES WILL_FAIL TRUE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND JMPXX_CLANGXX AND JMPXX_MSAN_LIBCXX_ROOT)
  add_test(NAME msan COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/msan_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/adversarial/msan_surface.cpp clean
    ${JMPXX_MSAN_LIBCXX_ROOT})
  add_test(NAME msan.teeth COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/msan_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/adversarial/msan_known_bad.cpp inject
    ${JMPXX_MSAN_LIBCXX_ROOT})
  set_tests_properties(msan.teeth PROPERTIES WILL_FAIL TRUE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND JMPXX_CLANGXX AND JMPXX_SCAN_BUILD)
  add_test(NAME static_analysis COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/static_analysis_check.sh
    ${JMPXX_SCAN_BUILD} ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/tests/adversarial/static_analysis_public.cpp)
  add_test(NAME static_analysis.teeth COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/static_analysis_check.sh
    ${JMPXX_SCAN_BUILD} ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/tests/adversarial/static_analysis_known_bad.cpp)
  set_tests_properties(static_analysis.teeth PROPERTIES WILL_FAIL TRUE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND JMPXX_PYTHON3 AND JMPXX_CLANGXX)
  add_test(NAME mutation.campaign COMMAND
    ${JMPXX_PYTHON3} ${CMAKE_SOURCE_DIR}/tests/scripts/mutation_campaign.py
    --cxx ${JMPXX_CLANGXX}
    --source-root ${CMAKE_SOURCE_DIR}
    --build-root ${CMAKE_BINARY_DIR}
    --floor 0.90
    --mode clean
    --report ${CMAKE_BINARY_DIR}/mutation-campaign.json)
  add_test(NAME mutation.campaign.teeth COMMAND
    ${JMPXX_PYTHON3} ${CMAKE_SOURCE_DIR}/tests/scripts/mutation_campaign.py
    --cxx ${JMPXX_CLANGXX}
    --source-root ${CMAKE_SOURCE_DIR}
    --build-root ${CMAKE_BINARY_DIR}
    --floor 0.90
    --mode inject)
  set_tests_properties(mutation.campaign PROPERTIES TIMEOUT 240)
  set_tests_properties(mutation.campaign.teeth PROPERTIES WILL_FAIL TRUE TIMEOUT 120)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND JMPXX_PYTHON3 AND JMPXX_CLANGXX
   AND JMPXX_LLVM_PROFDATA AND JMPXX_LLVM_COV)
  add_test(NAME adversarial.coverage COMMAND
    ${JMPXX_PYTHON3} ${CMAKE_SOURCE_DIR}/tests/scripts/coverage_floor.py
    --cxx ${JMPXX_CLANGXX}
    --profdata ${JMPXX_LLVM_PROFDATA}
    --cov ${JMPXX_LLVM_COV}
    --source-root ${CMAKE_SOURCE_DIR}
    --build-root ${CMAKE_BINARY_DIR}
    --mode clean
    --report ${CMAKE_BINARY_DIR}/adversarial-coverage.json)
  add_test(NAME adversarial.coverage.teeth COMMAND
    ${JMPXX_PYTHON3} ${CMAKE_SOURCE_DIR}/tests/scripts/coverage_floor.py
    --cxx ${JMPXX_CLANGXX}
    --profdata ${JMPXX_LLVM_PROFDATA}
    --cov ${JMPXX_LLVM_COV}
    --source-root ${CMAKE_SOURCE_DIR}
    --build-root ${CMAKE_BINARY_DIR}
    --mode inject)
  set_tests_properties(adversarial.coverage PROPERTIES TIMEOUT 90)
  set_tests_properties(adversarial.coverage.teeth PROPERTIES WILL_FAIL TRUE TIMEOUT 60)
endif()

# Compile-fail tier: misuse must not compile; the correct forms must. The check
# uses GCC/Clang flag spellings, so it runs on those compilers.
if(NOT MSVC)
  set(CF ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail)
  function(jmpxx_compile_case name src expect)
    add_test(NAME compile_fail.${name} COMMAND ${CMAKE_COMMAND}
      -DCXX=${CMAKE_CXX_COMPILER} -DSTD=c++20 -DINC=${CMAKE_SOURCE_DIR}/include
      -DSRC=${src} -DEXPECT=${expect}
      -P ${CMAKE_SOURCE_DIR}/cmake/compile_check.cmake)
  endfunction()
  jmpxx_compile_case(discard ${CF}/cf_discard.cpp fail)
  jmpxx_compile_case(ignore_site ${CF}/cf_ignore_site.cpp fail)
  jmpxx_compile_case(ok_use ${CF}/cf_ok_use.cpp succeed)
  jmpxx_compile_case(ok_propagate ${CF}/cf_ok_propagate.cpp succeed)
endif()

# Codegen (asm-golden) tier, pinned to g++-13. Real gates assert the committed
# golden and the absence of spills; the inverted cases assert the gate rejects a wrong
# golden and a spilling fixture.
# The goldens are pinned to g++-13 on x86-64 Linux, so the codegen tier runs only
# there; on any other platform the committed assembly would not match.
set(JMPXX_CODEGEN_HOST OFF)
if(JMPXX_GXX13 AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
  set(JMPXX_CODEGEN_HOST ON)
endif()
if(JMPXX_CODEGEN_HOST)
  set(FX ${CMAKE_SOURCE_DIR}/tests/asm_golden/fixtures)
  set(GD ${CMAKE_SOURCE_DIR}/goldens/x86_64)
  set(KB ${CMAKE_SOURCE_DIR}/goldens/known_bad)
  add_test(NAME codegen.value_return COMMAND jmpxx-verify codegen
    --fixture ${FX}/value_return.cpp --symbol jx_value_return
    --golden ${GD}/jx_value_return.s --forbid-spill --max-insns 8)
  add_test(NAME codegen.propagate_eight COMMAND jmpxx-verify codegen
    --fixture ${FX}/propagate_eight.cpp --symbol jx_propagate_eight
    --golden ${GD}/jx_propagate_eight.s --forbid-spill --max-insns 12)
  add_test(NAME codegen.try_branch COMMAND jmpxx-verify codegen
    --fixture ${FX}/try_branch.cpp --symbol jx_try_branch
    --golden ${GD}/jx_try_branch.s --max-insns 24)

  add_test(NAME codegen.teeth.golden COMMAND jmpxx-verify codegen
    --fixture ${FX}/value_return.cpp --symbol jx_value_return
    --golden ${KB}/jx_value_return.bad.s)
  set_tests_properties(codegen.teeth.golden PROPERTIES WILL_FAIL TRUE)

  add_test(NAME codegen.teeth.spill COMMAND jmpxx-verify codegen
    --fixture ${CMAKE_SOURCE_DIR}/tests/asm_golden/known_bad/spilly.cpp
    --symbol jx_spilly --golden ${CMAKE_BINARY_DIR}/spilly.s --update --forbid-spill)
  set_tests_properties(codegen.teeth.spill PROPERTIES WILL_FAIL TRUE)

  # Release-diff: the dual-personality release check. The rich and minimal policies
  # generate identical code in a release build. The inverted case uses a fixture whose
  # diagnostic call survives into release, by code or by a leaked source string, and
  # that fixture must fail the equality the gate asserts.
  add_test(NAME release_diff COMMAND jmpxx-verify release-diff
    --fixture ${FX}/policy_release.cpp --symbol-a jx_min_op --symbol-b jx_rich_op
    --forbid-string policy_release)
  add_test(NAME release_diff.teeth COMMAND jmpxx-verify release-diff
    --fixture ${KB}/rich_leaky.cpp --symbol-a jx_min_op --symbol-b jx_rich_leaky
    --forbid-string rich_leaky)
  set_tests_properties(release_diff.teeth PROPERTIES WILL_FAIL TRUE)
endif()

# Binary-size-delta gate: the library adds no bytes over the hand-written branch in
# the ship configuration. The fixture and the baseline compile to objects of equal
# section size, so the delta is zero; the inverted case is a bloated fixture whose
# object is larger and must fail the zero budget. Deterministic for a fixed compiler,
# so it runs on the codegen host where g++-13 is pinned.
set(BF ${CMAKE_SOURCE_DIR}/benchmarks/fixtures)
if(JMPXX_CODEGEN_HOST)
  add_test(NAME size_delta COMMAND jmpxx-verify size-delta
    --fixture ${BF}/ship_jmpxx.cpp --baseline ${BF}/ship_handwritten.cpp
    --max-text-delta 0 --max-total-delta 0)
  add_test(NAME size_delta.teeth COMMAND jmpxx-verify size-delta
    --fixture ${BF}/known_bad/ship_bloated.cpp --baseline ${BF}/ship_handwritten.cpp
    --max-text-delta 0 --max-total-delta 0)
  set_tests_properties(size_delta.teeth PROPERTIES WILL_FAIL TRUE)
endif()

# ABI layout gate: the observable layout of the frozen public types does not change
# within the major version. The probe compiles the descriptor fixture in the ship
# configuration and diffs the emitted layout against the committed golden; the inverted
# case points the gate at a golden claiming a changed layout, which must fail. Pinned
# to the LP64 codegen host, where the committed golden's pointer width and type sizes
# hold; the static tier backstops the architecture-independent facts on every cell.
if(JMPXX_CODEGEN_HOST)
  add_test(NAME abi_layout COMMAND jmpxx-verify abi-layout
    --fixture ${CMAKE_SOURCE_DIR}/tests/abi/layout_descriptor.cpp
    --golden ${CMAKE_SOURCE_DIR}/goldens/abi/layout_lp64.txt)
  add_test(NAME abi_layout.teeth COMMAND jmpxx-verify abi-layout
    --fixture ${CMAKE_SOURCE_DIR}/tests/abi/layout_descriptor.cpp
    --golden ${CMAKE_SOURCE_DIR}/goldens/abi/known_bad/layout_lp64.bad.txt)
  set_tests_properties(abi_layout.teeth PROPERTIES WILL_FAIL TRUE)
endif()

# Single-header amalgamation gate: the committed single headers match the modular
# source (regenerate and diff), the core header builds and runs freestanding, and the
# full header builds and runs a hosted consumer. Pinned to g++-13 because the
# freestanding compile needs the one libstdc++ that honors -ffreestanding, and needs
# python3 for the generator.
if(JMPXX_GXX13 AND JMPXX_PYTHON3 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME amalgamation COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/amalgamation_check.sh
    ${CMAKE_SOURCE_DIR} ${JMPXX_GXX13})
endif()

# The acceptance sweep's own inverted self-test. acceptance.py aggregates the tiers
# and gates into one release verdict; its --self-test checks that verdict logic fails a
# failed test and a gate that has no inverted self-test. Pure logic, no nested CTest,
# so it runs here as a tier; the full sweep is run by hand or in continuous
# integration against a built tree, never as a test, to avoid recursion.
if(JMPXX_PYTHON3)
  add_test(NAME acceptance.self_test COMMAND
    ${JMPXX_PYTHON3} ${CMAKE_SOURCE_DIR}/verify/acceptance.py --self-test)
endif()

# Compile-cost gate: the template-instantiation count, the deterministic compile-cost
# metric, stays within budget. The gate uses Clang for -ftime-trace; the count is identical
# across runs and across Clang versions, so the gate never flakes. The inverted case is an
# instantiation-heavy fixture far above the budget.
if(JMPXX_CLANGXX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME compile_cost COMMAND jmpxx-verify compile-cost
    --cxx ${JMPXX_CLANGXX} --fixture ${BF}/ship_jmpxx.cpp
    --baseline ${BF}/ship_handwritten.cpp --runs 3 --max-instantiations 320)
  add_test(NAME compile_cost.teeth COMMAND jmpxx-verify compile-cost
    --cxx ${JMPXX_CLANGXX} --fixture ${BF}/known_bad/instantiation_heavy.cpp
    --baseline ${BF}/ship_handwritten.cpp --runs 1 --max-instantiations 320)
  set_tests_properties(compile_cost.teeth PROPERTIES WILL_FAIL TRUE)
endif()

# Freestanding tier: the minimal core builds and runs with exceptions and RTTI
# disabled and its include graph pulls no hosted header. Pinned to g++-13 on
# Linux, the one toolchain whose libstdc++ honors -ffreestanding.
if(JMPXX_GXX13 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME freestanding COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/freestanding_check.sh
    ${JMPXX_GXX13} ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/freestanding/fs_core.cpp ${CMAKE_BINARY_DIR}/fs_core)
endif()

# Sanitizer tier: the property sequences run clean under Address and
# UndefinedBehavior sanitizers.
if(UNIX AND JMPXX_CLANGXX)
  add_test(NAME sanitizer COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/sanitizer_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/property/property_tests.cpp)
endif()

# Doc-claim tier: a documented cost the harness reports must appear verbatim in the
# reference, so a documented value cannot drift from the measured one. The inverted
# case points the same check at a document that states a cost the harness does not
# report, which must fail; the other documented cost, size, codegen, perf, and layout
# claims are each backed by their own gate above, every one checked against a
# known-bad input.
if(UNIX)
  add_test(NAME doc_claim COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/doc_claim_check.sh
    $<TARGET_FILE:jmpxx-verify> ${CMAKE_SOURCE_DIR}/docs/reference/propagation-levels.md)
  add_test(NAME doc_claim.teeth COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/doc_claim_check.sh
    $<TARGET_FILE:jmpxx-verify> ${CMAKE_SOURCE_DIR}/tests/doc_claim/known_bad/propagation_levels.bad.md)
  set_tests_properties(doc_claim.teeth PROPERTIES WILL_FAIL TRUE)
endif()

# Unwind-execution tier for the experimental non-local unwind arm. It is built at several
# optimization levels because a forced unwind that
# elides a cleanup landing pad or lands at the wrong frame is exposed only under
# optimization; an -O0-only run would pass a regressed arm that silently skips
# destructors at -O2. The arm requires exception cleanup tables, which are the default,
# so the tier is not built under the no-exceptions configuration. The MSVC structured-
# exception backend is exercised in a separate step below, so the tier is built on GCC
# and Clang, which also cover MinGW.
if(NOT MSVC)
  foreach(opt 0 2 3)
    add_executable(jmpxx_unwind_exec_O${opt} unwind/unwind_exec.cpp)
    target_link_libraries(jmpxx_unwind_exec_O${opt} PRIVATE jmpxx::jmpxx)
    target_compile_options(jmpxx_unwind_exec_O${opt} PRIVATE -O${opt} ${JMPXX_TEST_WARNINGS})
    add_test(NAME unwind.exec.O${opt} COMMAND jmpxx_unwind_exec_O${opt})
  endforeach()

  # A swallowing catch-all on the escape path is never a silent wrong landing. A
  # non-cooperative catch(...) that does not rethrow is handled differently by different
  # C++ runtimes: libstdc++ and libc++abi let it consume the escape, which the arm turns
  # into a defined termination, while libcxxrt transits it so the escape reaches its
  # landing. The wrapper passes on either safe outcome, a fail-fast signal or a clean
  # exit, and fails only if the escape is silently lost. Built at -O2.
  add_executable(jmpxx_unwind_swallow unwind/swallow.cpp)
  target_link_libraries(jmpxx_unwind_swallow PRIVATE jmpxx::jmpxx)
  target_compile_options(jmpxx_unwind_swallow PRIVATE -O2 ${JMPXX_TEST_WARNINGS})
  add_test(NAME unwind.swallow_handled COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/expect_swallow_handled.sh
    $<TARGET_FILE:jmpxx_unwind_swallow>)
endif()

# Compile-fail: the arm refuses a configuration without unwind cleanup tables. Including
# jmpxx/unwind.hpp under -fno-exceptions is a compile-time refusal, and the matching
# correct use under exceptions compiles.
if(NOT MSVC)
  add_test(NAME unwind.compile_fail_no_tables COMMAND ${CMAKE_COMMAND}
    -DCXX=${CMAKE_CXX_COMPILER} -DSTD=c++20 -DINC=${CMAKE_SOURCE_DIR}/include
    -DSRC=${CMAKE_SOURCE_DIR}/tests/unwind/no_tables.cpp -DEXPECT=fail
    -DFLAGS=-fno-exceptions
    -P ${CMAKE_SOURCE_DIR}/cmake/compile_check.cmake)
  add_test(NAME unwind.compile_ok_with_tables COMMAND ${CMAKE_COMMAND}
    -DCXX=${CMAKE_CXX_COMPILER} -DSTD=c++20 -DINC=${CMAKE_SOURCE_DIR}/include
    -DSRC=${CMAKE_SOURCE_DIR}/tests/unwind/no_tables.cpp -DEXPECT=succeed
    -P ${CMAKE_SOURCE_DIR}/cmake/compile_check.cmake)
endif()

# Opt-in scan: no portable public header reaches the arm, so a consumer that never
# includes jmpxx/unwind.hpp never compiles it. The scan checks that the arm is off every
# default path. Pure text scan, so it runs anywhere bash does.
if(UNIX)
  add_test(NAME unwind.opt_in_scan COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/unwind_opt_in_check.sh
    ${CMAKE_SOURCE_DIR}/include/jmpxx)
endif()

# Unwind sad-path benchmark and the destructor-count surface, driven through the harness.
# The committed run asserts the forced-unwind sad path stays within its determinism bound
# and reports the distribution beside a C++ throw at
# the same depth. The inverted run injects a non-deterministic cleanup, which drives the
# ratio past the bound and must fail the same gate's negative path.
add_test(NAME unwind.benchmark COMMAND jmpxx-verify unwind --iters 40000)
add_test(NAME unwind.benchmark.teeth COMMAND jmpxx-verify unwind --inject-jitter --iters 20000)
set_tests_properties(unwind.benchmark.teeth PROPERTIES WILL_FAIL TRUE TIMEOUT 180)

# Payload-boundary fuzz: any value an eject carries, including one derived from
# adversarial input, must cross to the landing as a defined failure rather than
# undefined behavior. libFuzzer drives the eject and landing under the address and
# undefined-behavior sanitizers, so any UB on any input is a crash. libFuzzer is a Clang
# facility on Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND JMPXX_CLANGXX)
  add_test(NAME unwind.fuzz COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/fuzz_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/tests/fuzz/fuzz_unwind.cpp 15)
  set_tests_properties(unwind.fuzz PROPERTIES TIMEOUT 120)
endif()

# The compile-and-measure probes each drive a fixture through a fixed temporary file,
# so two of them running at once would race on it. A shared resource lock serializes
# them against each other, which keeps the suite correct under `ctest -j` while every
# other tier still parallelizes. Continuous integration runs the suite serially, where
# the race cannot arise; this keeps a parallel local run correct too.
foreach(t codegen.value_return codegen.propagate_eight codegen.try_branch
          codegen.teeth.golden codegen.teeth.spill release_diff release_diff.teeth
          size_delta size_delta.teeth compile_cost compile_cost.teeth
          abi_layout abi_layout.teeth)
  if(TEST ${t})
    set_tests_properties(${t} PROPERTIES RESOURCE_LOCK jmpxx_verify_tmp)
  endif()
endforeach()
