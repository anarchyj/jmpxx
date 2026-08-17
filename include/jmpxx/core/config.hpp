// SPDX-License-Identifier: MIT
// jmpxx core configuration: language gate, version, and the portable attribute,
// branch-hint, and hardening macros the freestanding core relies on. This header
// pulls in no standard library header; it uses only the preprocessor and
// compiler-provided feature-test facilities. Target detection lives one layer
// down in platform/detect.hpp, the single home for the compiler's predefined
// macros, so this header asks for compiler identity through JMPXX_COMPILER_*
// rather than reading the raw macros itself.
#ifndef JMPXX_CORE_CONFIG_HPP
#define JMPXX_CORE_CONFIG_HPP

#include "jmpxx/platform/detect.hpp"

#define JMPXX_VERSION_MAJOR 0
#define JMPXX_VERSION_MINOR 1
#define JMPXX_VERSION_PATCH 4

// A single comparable integer, major*10000 + minor*100 + patch, so a consumer can
// gate on a version in one line: #if JMPXX_VERSION >= 200. The scheme caps minor and
// patch at 99. JMPXX_VERSION_STRING is derived from the components so the two cannot
// drift.
#define JMPXX_VERSION \
  (JMPXX_VERSION_MAJOR * 10000 + JMPXX_VERSION_MINOR * 100 + JMPXX_VERSION_PATCH)
#define JMPXX_DETAIL_STRINGIZE2(x) #x
#define JMPXX_DETAIL_STRINGIZE(x) JMPXX_DETAIL_STRINGIZE2(x)
#define JMPXX_VERSION_STRING                                       \
  JMPXX_DETAIL_STRINGIZE(JMPXX_VERSION_MAJOR)                      \
  "." JMPXX_DETAIL_STRINGIZE(JMPXX_VERSION_MINOR) "." JMPXX_DETAIL_STRINGIZE( \
      JMPXX_VERSION_PATCH)

// JMPXX_CPLUSPLUS is the active standard version, normalized for MSVC in
// platform/detect.hpp. C++20 is the floor for every supported configuration.
#if !defined(__cplusplus) || JMPXX_CPLUSPLUS < 202002L
#error "jmpxx requires C++20 or later."
#endif

// feature-test wrappers (guarded so an absent facility is not a hard error)
#if defined(__has_builtin)
#define JMPXX_HAS_BUILTIN(x) __has_builtin(x)
#else
#define JMPXX_HAS_BUILTIN(x) 0
#endif

#if defined(__has_cpp_attribute)
#define JMPXX_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define JMPXX_HAS_CPP_ATTRIBUTE(x) 0
#endif

// [[nodiscard]] with a message where the message form is available. A
// produced failure must not be dropped silently; this is the type-level half of
// that guarantee, and the propagation site enforces the other half.
#if JMPXX_HAS_CPP_ATTRIBUTE(nodiscard) >= 201907L
#define JMPXX_NODISCARD(msg) [[nodiscard(msg)]]
#elif JMPXX_HAS_CPP_ATTRIBUTE(nodiscard)
#define JMPXX_NODISCARD(msg) [[nodiscard]]
#else
#define JMPXX_NODISCARD(msg)
#endif

// forced inlining for the thin wrappers whose only job is to vanish. Used
// sparingly; the optimizer is trusted for everything else.
#if JMPXX_COMPILER_GCC || JMPXX_COMPILER_CLANG
#define JMPXX_ALWAYS_INLINE inline __attribute__((always_inline))
#elif JMPXX_COMPILER_MSVC
#define JMPXX_ALWAYS_INLINE __forceinline
#else
#define JMPXX_ALWAYS_INLINE inline
#endif

// Forced non-inlining. The unwind arm needs one frame to stay a real stack frame
// regardless of optimization, so that capturing the landing frame's address by a
// fixed unwind depth stays correct; inlining it would shift that depth and land the
// escape at the wrong frame. Used only where a stable frame identity is required.
#if JMPXX_COMPILER_GCC || JMPXX_COMPILER_CLANG
#define JMPXX_NOINLINE __attribute__((noinline))
#elif JMPXX_COMPILER_MSVC
#define JMPXX_NOINLINE __declspec(noinline)
#else
#define JMPXX_NOINLINE
#endif

// expression-level branch hints for the propagation fast path. The happy
// path is the predicted one. These compile to nothing where unsupported.
#if JMPXX_COMPILER_GCC || JMPXX_COMPILER_CLANG
#define JMPXX_LIKELY(x) (__builtin_expect(static_cast<bool>(x), 1))
#define JMPXX_UNLIKELY(x) (__builtin_expect(static_cast<bool>(x), 0))
#else
#define JMPXX_LIKELY(x) (static_cast<bool>(x))
#define JMPXX_UNLIKELY(x) (static_cast<bool>(x))
#endif

// Hardening mode. The vocabulary follows the standard-library hardening names:
// none removes narrow-contract checks, fast checks the narrow value/error accessors,
// and extensive also validates defensive boundary invariants before dispatch. The
// old binary JMPXX_HARDENED switch remains source-compatible: 0 maps to none, any
// nonzero value maps to fast, and a consumer that only reads JMPXX_HARDENED still
// sees 0 or 1.
#define JMPXX_HARDENING_NONE 0
#define JMPXX_HARDENING_FAST 1
#define JMPXX_HARDENING_EXTENSIVE 2

#if !defined(JMPXX_HARDENING_MODE)
#if defined(JMPXX_HARDENED)
#if JMPXX_HARDENED
#define JMPXX_HARDENING_MODE JMPXX_HARDENING_FAST
#else
#define JMPXX_HARDENING_MODE JMPXX_HARDENING_NONE
#endif
#elif defined(NDEBUG)
#define JMPXX_HARDENING_MODE JMPXX_HARDENING_NONE
#else
#define JMPXX_HARDENING_MODE JMPXX_HARDENING_FAST
#endif
#endif

#if JMPXX_HARDENING_MODE != JMPXX_HARDENING_NONE && \
    JMPXX_HARDENING_MODE != JMPXX_HARDENING_FAST && \
    JMPXX_HARDENING_MODE != JMPXX_HARDENING_EXTENSIVE
#error "JMPXX_HARDENING_MODE must be JMPXX_HARDENING_NONE, JMPXX_HARDENING_FAST, or JMPXX_HARDENING_EXTENSIVE."
#endif

#if defined(JMPXX_HARDENED)
#if (JMPXX_HARDENED && JMPXX_HARDENING_MODE < JMPXX_HARDENING_FAST) || \
    (!JMPXX_HARDENED && JMPXX_HARDENING_MODE >= JMPXX_HARDENING_FAST)
#error "JMPXX_HARDENED conflicts with JMPXX_HARDENING_MODE."
#endif
#else
#if JMPXX_HARDENING_MODE >= JMPXX_HARDENING_FAST
#define JMPXX_HARDENED 1
#else
#define JMPXX_HARDENED 0
#endif
#endif

#define JMPXX_HARDENING_FAST_ENABLED \
  (JMPXX_HARDENING_MODE >= JMPXX_HARDENING_FAST)
#define JMPXX_HARDENING_EXTENSIVE_ENABLED \
  (JMPXX_HARDENING_MODE >= JMPXX_HARDENING_EXTENSIVE)

// diagnostic-layer switch. JMPXX_DIAGNOSTICS_ENABLED=1 turns on the debug-only
// diagnostic layer: the rich policy captures a failure's origin and the causal
// chain it accumulates as it propagates, held out of band. When 0 the entire
// layer compiles to nothing, the rich policy's representation and codegen equal
// the minimal policy's, and the propagation hop hook below expands to nothing.
// Default: on unless NDEBUG, matching the debug/release split. Override with
// -DJMPXX_DIAGNOSTICS_ENABLED. The freestanding minimal core does not depend on
// this switch; it gates only the hosted diagnostic layer and the hop hook, so
// the minimal policy stays freestanding and zero-overhead regardless of its value.
#if !defined(JMPXX_DIAGNOSTICS_ENABLED)
#if defined(NDEBUG)
#define JMPXX_DIAGNOSTICS_ENABLED 0
#else
#define JMPXX_DIAGNOSTICS_ENABLED 1
#endif
#endif

// optional stack-trace capture for the diagnostic layer. JMPXX_STACKTRACE=1 makes
// a captured failure also record the return addresses of its creation site, on
// platforms where a fenced capturer is available. Off by default and meaningful
// only when JMPXX_DIAGNOSTICS_ENABLED is on. Symbolizing the addresses is an
// offline concern; the capture path takes no lock and allocates nothing. Kept off
// by default because portable, dependency-free trace capture is not universal.
#if !defined(JMPXX_STACKTRACE)
#define JMPXX_STACKTRACE 0
#endif

#endif  // JMPXX_CORE_CONFIG_HPP
