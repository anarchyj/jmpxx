# SPDX-License-Identifier: MIT
# Tiers for the experimental non-local unwind arm. The arm's correctness rests on the
# platform unwinder, the optimizer, and the C++ runtime rather than on the library's own
# control flow, so it carries more tiers than any other capability: execution at several
# optimization levels, link-time optimization, the emitted unwind metadata, concurrency,
# reentrancy, the per-runtime behaviour matrix, long-run stress, thread scaling, the
# sad-path benchmark, and the payload-boundary fuzz. Each gate here has an inverted case.

# Execution at several optimization levels, because a forced unwind that elides a cleanup
# landing pad or lands at the wrong frame is exposed only under optimization: an -O0-only
# run would pass an arm that silently skips destructors at -O2. Built on GCC and Clang,
# whose flags spell the levels; the MSVC backend is exercised by its own job.
if(NOT MSVC)
  foreach(opt 0 2 3)
    add_executable(jmpxx_unwind_exec_O${opt} unwind_exec.cpp)
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
  add_executable(jmpxx_unwind_swallow swallow.cpp)
  target_link_libraries(jmpxx_unwind_swallow PRIVATE jmpxx::jmpxx)
  target_compile_options(jmpxx_unwind_swallow PRIVATE -O2 ${JMPXX_TEST_WARNINGS})
  add_test(NAME unwind.swallow_handled COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/expect_swallow_handled.sh
    $<TARGET_FILE:jmpxx_unwind_swallow>)
endif()

# Link-time and whole-program optimization over the arm. The per-translation-unit tiers
# above compile the landing and the frames together; this one keeps them apart until the
# link, which is where the optimizer first sees the whole escape and could re-derive the
# property the arm depends on staying unprovable. Static linking and section garbage
# collection are included because they change how the unwinder finds the frame tables.
# The inverted run builds the regressed arm from known_bad/, whose eject a caller can
# prove cannot unwind, and the destructor imbalance that causes must fail the tier.
if(UNIX AND NOT MSVC)
  add_test(NAME unwind.lto COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/unwind_lto_check.sh
    ${CMAKE_CXX_COMPILER} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR} clean)
  add_test(NAME unwind.lto.teeth COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/unwind_lto_check.sh
    ${CMAKE_CXX_COMPILER} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR} inject)
  set_tests_properties(unwind.lto PROPERTIES TIMEOUT 600)
  set_tests_properties(unwind.lto.teeth PROPERTIES WILL_FAIL TRUE TIMEOUT 600)
endif()

# Unwind-metadata audit. Destructor counts prove the cleanups ran where the program ran;
# this reads the exception tables the runtime consults, so a frame that lost its cleanup
# landing pads is caught in the binary itself. The subject is the escape chain at -O2,
# and the inverted subject is the same chain built against the regressed arm, whose
# frames lose their landing pads.
if(UNIX AND NOT MSVC)
  add_executable(jmpxx_unwind_metadata_subject lto_frames.cpp lto_landing.cpp)
  add_executable(jmpxx_unwind_metadata_regressed lto_frames.cpp lto_landing.cpp)
  foreach(t jmpxx_unwind_metadata_subject jmpxx_unwind_metadata_regressed)
    target_link_libraries(${t} PRIVATE jmpxx::jmpxx)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${t} PRIVATE -O2 ${JMPXX_TEST_WARNINGS})
  endforeach()
  target_compile_definitions(jmpxx_unwind_metadata_regressed PRIVATE
    JMPXX_TEST_REGRESSED_ARM=1 JMPXX_KNOWN_BAD_NOTHROW_EJECT=1)
  # The subject runs as its own case too: the same binary the audit reads is one that
  # delivers its escapes, so the static and the live evidence are about one artifact.
  add_test(NAME unwind.exec.cross_unit COMMAND jmpxx_unwind_metadata_subject)
  # The audit decodes ELF sections directly, so it is meaningful only where the object
  # format is ELF. Apple platforms are Mach-O and their compact unwind is a different
  # encoding this does not read; the destructor-count tiers above still cover them, and
  # the emitted metadata there is unaudited. Running it anyway would not merely skip: the
  # audit would raise on the file header, and because the inverted case is registered
  # WILL_FAIL, that raise made the teeth test pass without ever reading a landing pad.
  if(JMPXX_PYTHON3 AND NOT APPLE)
    add_test(NAME unwind.metadata COMMAND ${JMPXX_PYTHON3}
      ${CMAKE_SOURCE_DIR}/tests/scripts/unwind_metadata_audit.py
      $<TARGET_FILE:jmpxx_unwind_metadata_subject>
      --require chain_top --require recursive_chain
      --report ${CMAKE_BINARY_DIR}/unwind-metadata.json)
    add_test(NAME unwind.metadata.teeth COMMAND ${JMPXX_PYTHON3}
      ${CMAKE_SOURCE_DIR}/tests/scripts/unwind_metadata_audit.py
      $<TARGET_FILE:jmpxx_unwind_metadata_regressed>
      --require chain_top --require recursive_chain)
    set_tests_properties(unwind.metadata.teeth PROPERTIES WILL_FAIL TRUE)
  endif()
endif()

# Concurrent escapes. Every thread runs its own landing scopes, nests them, and mixes
# success with failure, so the per-thread landing stack and the per-scope unwind object
# are exercised rather than trusted. The inverted build shares one landing stack across
# threads, which the tier must catch. The thread sanitizer runs the same fixture, where a
# race in the arm's own state is a finding.
if(NOT MSVC)
  add_executable(jmpxx_unwind_concurrent concurrent.cpp)
  add_executable(jmpxx_unwind_concurrent_shared concurrent.cpp)
  foreach(t jmpxx_unwind_concurrent jmpxx_unwind_concurrent_shared)
    target_link_libraries(${t} PRIVATE jmpxx::jmpxx Threads::Threads)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${t} PRIVATE -O2 ${JMPXX_TEST_WARNINGS})
  endforeach()
  target_compile_definitions(jmpxx_unwind_concurrent_shared PRIVATE
    JMPXX_TEST_REGRESSED_ARM=1 JMPXX_KNOWN_BAD_SHARED_CARRIER=1)
  add_test(NAME unwind.concurrent COMMAND jmpxx_unwind_concurrent --threads 8 --iterations 400)
  add_test(NAME unwind.concurrent.teeth COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/expect_command_failure.sh
    $<TARGET_FILE:jmpxx_unwind_concurrent_shared> --threads 8 --iterations 400)
  set_tests_properties(unwind.concurrent PROPERTIES TIMEOUT 300)
  set_tests_properties(unwind.concurrent.teeth PROPERTIES WILL_FAIL TRUE TIMEOUT 300)
endif()
if(UNIX AND JMPXX_CLANGXX)
  add_test(NAME unwind.concurrent.sanitizer_thread COMMAND bash
    ${CMAKE_SOURCE_DIR}/tests/scripts/sanitizer_diag_check.sh
    ${JMPXX_CLANGXX} ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}
    thread ${CMAKE_CURRENT_SOURCE_DIR}/concurrent.cpp)
  set_tests_properties(unwind.concurrent.sanitizer_thread PROPERTIES TIMEOUT 600)
endif()

# The reentrancy contract: what happens when code that runs while an escape is unwinding
# escapes again, and every other way an eject can be misdirected. Each case declares
# whether it lands or terminates, and a case that lands where it must terminate is the
# silent-wrong-landing shape this tier exists to catch. The inverted run makes the
# refusing cases return success instead, which the driver must fail.
if(NOT MSVC)
  add_executable(jmpxx_unwind_reentrancy reentrancy.cpp)
  target_link_libraries(jmpxx_unwind_reentrancy PRIVATE jmpxx::jmpxx Threads::Threads)
  target_compile_options(jmpxx_unwind_reentrancy PRIVATE -O2 ${JMPXX_TEST_WARNINGS})
  if(UNIX)
    add_test(NAME unwind.reentrancy COMMAND bash
      ${CMAKE_SOURCE_DIR}/tests/scripts/unwind_reentrancy_check.sh
      $<TARGET_FILE:jmpxx_unwind_reentrancy> clean)
    add_test(NAME unwind.reentrancy.teeth COMMAND bash
      ${CMAKE_SOURCE_DIR}/tests/scripts/unwind_reentrancy_check.sh
      $<TARGET_FILE:jmpxx_unwind_reentrancy> inject)
    set_tests_properties(unwind.reentrancy.teeth PROPERTIES WILL_FAIL TRUE)
  endif()
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

# The per-runtime behaviour matrix. How an escape meets a handler or a barrier on its
# path is the runtime's answer, not the arm's, and the runtimes disagree, so each case
# runs as its own process and the harness records what came back. The fixture is portable
# so the same cases can be run on the cells where the harness is not built, and the
# inverted run makes a case fabricate a success, which the matrix must reject.
if(NOT MSVC)
  add_executable(jmpxx_unwind_runtime_matrix runtime_matrix.cpp)
  target_link_libraries(jmpxx_unwind_runtime_matrix PRIVATE jmpxx::jmpxx)
  target_compile_options(jmpxx_unwind_runtime_matrix PRIVATE -O2 ${JMPXX_TEST_WARNINGS})
  add_test(NAME unwind.matrix COMMAND jmpxx-verify unwind-matrix
    --fixture $<TARGET_FILE:jmpxx_unwind_runtime_matrix>)
  add_test(NAME unwind.matrix.teeth COMMAND jmpxx-verify unwind-matrix
    --fixture $<TARGET_FILE:jmpxx_unwind_runtime_matrix> --inject-silent-loss)
  set_tests_properties(unwind.matrix.teeth PROPERTIES WILL_FAIL TRUE)
endif()

# Long-run stress and thread scaling, both driven through the harness so their numbers
# land in the structured report. The stress campaign is where a rare leak or a cleanup
# imbalance shows up; the scaling probe answers whether the escape stays as parallel as
# the platform's own unwinder, which is the question a server or engine asks before
# putting the arm on a request path.
add_test(NAME unwind.stress COMMAND jmpxx-verify unwind-stress
  --iterations 250000 --threads 4 --seeds 3)
add_test(NAME unwind.stress.teeth COMMAND jmpxx-verify unwind-stress
  --iterations 20000 --inject-imbalance)
set_tests_properties(unwind.stress PROPERTIES TIMEOUT 600)
set_tests_properties(unwind.stress.teeth PROPERTIES WILL_FAIL TRUE TIMEOUT 300)

add_test(NAME unwind.scale COMMAND jmpxx-verify unwind-scale --max-threads 8 --iters 3000)
add_test(NAME unwind.scale.teeth COMMAND jmpxx-verify unwind-scale
  --max-threads 8 --iters 2000 --inject-serialization)
set_tests_properties(unwind.scale PROPERTIES TIMEOUT 600)
set_tests_properties(unwind.scale.teeth PROPERTIES WILL_FAIL TRUE TIMEOUT 300)

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
