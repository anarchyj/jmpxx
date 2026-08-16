# SPDX-License-Identifier: MIT
add_executable(jmpxx-verify main.cpp unwind_probes.cpp)
# Threads are a harness dependency, not a library one: the unwind stress and scaling
# probes drive escapes from many threads at once.
find_package(Threads REQUIRED)
target_link_libraries(jmpxx-verify PRIVATE jmpxx::jmpxx Threads::Threads)
# The harness is built at C++23 so it can exercise the std::expected bridge through
# the interop probe where the standard library provides <expected>; the bridge
# self-gates, so the harness still builds where it does not.
target_compile_features(jmpxx-verify PRIVATE cxx_std_23)
# The harness needs direct visibility into the library's live behavior, so it is
# built with the diagnostic layer on regardless of build type: the policies,
# diagnostics, and alloc probes exercise the rich policy's out-of-band store directly. The
# release-diff probe compiles its fixtures in a release configuration of its own, so the
# release zero-cost check does not depend on how the harness itself is built.
target_compile_definitions(jmpxx-verify PRIVATE
  JMPXX_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
  JMPXX_BENCH_KERNELS_DIR="${CMAKE_SOURCE_DIR}/benchmarks/kernels"
  JMPXX_DIAGNOSTICS_ENABLED=1)
if(NOT MSVC)
  target_compile_options(jmpxx-verify PRIVATE -Wall -Wextra)
endif()
