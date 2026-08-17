// SPDX-License-Identifier: MIT
// jmpxx, single-header amalgamation (full hosted surface)
//
// This file is generated from the modular headers under include/jmpxx by
// packaging/amalgamate.py. Do not edit it by hand; edit the modular headers
// and regenerate. Continuous integration regenerates and diffs it, so a stale
// single header fails the build.
#ifndef JMPXX_AMALGAMATED_HPP
#define JMPXX_AMALGAMATED_HPP

// from jmpxx/core.hpp
// SPDX-License-Identifier: MIT
// The minimal, freestanding core of jmpxx: the value-or-error transport, the
// minimal error representation, and single-construct propagation.
//
// This is the include boundary. Everything hosted, the diagnostic layer, the interop
// bridges, the reflection layer, and the experimental escape, lives under a separate
// header and is never reached from here, which is what keeps the core usable where
// the standard library is not. docs/reference/policies.md states what that promises a
// consumer; tests/scripts/include_boundary_check.sh is what holds it.
#ifndef JMPXX_CORE_HPP
#define JMPXX_CORE_HPP

// from jmpxx/core/config.hpp
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

// from jmpxx/platform/detect.hpp
// SPDX-License-Identifier: MIT
// Platform, architecture, and compiler detection: the only place the library reads
// compiler predefined macros. Every other header asks for a target fact
// through the JMPXX_COMPILER_*, JMPXX_OS_*, and JMPXX_ARCH_* tokens defined here,
// so a raw `_MSC_VER`, `__x86_64__`, or `_WIN32` appearing anywhere outside this
// platform unit is a fence violation the platform-fence scan rejects. Centralizing
// detection makes that fence enforceable and gives the experimental unwind arm one
// stable place to ask which ABI it is on.
//
// It uses only the preprocessor, so it costs a consumer nothing to include and is
// safe anywhere, including on the minimal core's path.
#ifndef JMPXX_PLATFORM_DETECT_HPP
#define JMPXX_PLATFORM_DETECT_HPP

// Compiler identity. Exactly one of these is 1. Clang is checked before GCC
// because Clang defines __GNUC__ for compatibility, and the MSVC test excludes
// Clang because clang-cl defines _MSC_VER while behaving like Clang for intrinsics
// and inline assembly, so clang-cl is reported as Clang, which matches the builtins
// it accepts.
#if defined(__clang__)
#define JMPXX_COMPILER_CLANG 1
#else
#define JMPXX_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define JMPXX_COMPILER_GCC 1
#else
#define JMPXX_COMPILER_GCC 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define JMPXX_COMPILER_MSVC 1
#else
#define JMPXX_COMPILER_MSVC 0
#endif

// The active C++ standard version. MSVC reports __cplusplus as 199711L unless
// /Zc:__cplusplus is passed, so the real version is read from _MSVC_LANG there.
#if defined(_MSVC_LANG)
#define JMPXX_CPLUSPLUS _MSVC_LANG
#else
#define JMPXX_CPLUSPLUS __cplusplus
#endif

// Whether C++ exceptions are enabled in this translation unit. GCC and Clang
// define __cpp_exceptions (and the historical __EXCEPTIONS); MSVC defines
// _CPPUNWIND under /EH. The exception interop bridge asks for this through the one
// token so the bridge header carries no raw compiler macro.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define JMPXX_HAS_EXCEPTIONS 1
#else
#define JMPXX_HAS_EXCEPTIONS 0
#endif

// Whether RTTI is enabled in this translation unit. Type-erased dispatch must not
// rely on it, but the configuration matrix records the dimension explicitly.
#if defined(__GXX_RTTI) || defined(__cpp_rtti) || defined(_CPPRTTI)
#define JMPXX_HAS_RTTI 1
#else
#define JMPXX_HAS_RTTI 0
#endif

// Operating system and environment. At most one hosted OS is 1. A freestanding or
// bare-metal target matches none of them and is reported by JMPXX_OS_NONE, and
// JMPXX_OS_HOSTED is the union so a header can ask "is there an OS here" without
// listing every one.
#if defined(_WIN32)
#define JMPXX_OS_WINDOWS 1
#else
#define JMPXX_OS_WINDOWS 0
#endif

// __APPLE__ with __MACH__ is Darwin, the kernel under macOS and the other Apple
// systems; jmpxx treats them as one OS cell.
#if defined(__APPLE__) && defined(__MACH__)
#define JMPXX_OS_MACOS 1
#else
#define JMPXX_OS_MACOS 0
#endif

#if defined(__linux__)
#define JMPXX_OS_LINUX 1
#else
#define JMPXX_OS_LINUX 0
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
#define JMPXX_OS_BSD 1
#else
#define JMPXX_OS_BSD 0
#endif

#if JMPXX_OS_WINDOWS || JMPXX_OS_MACOS || JMPXX_OS_LINUX || JMPXX_OS_BSD
#define JMPXX_OS_HOSTED 1
#define JMPXX_OS_NONE 0
#else
#define JMPXX_OS_HOSTED 0
#define JMPXX_OS_NONE 1
#endif

// Architecture. At most one is 1. The MSVC spellings (_M_X64, _M_ARM64, _M_ARM) sit
// beside the GCC/Clang ones so the detection holds on every compiler in the matrix.
#if defined(__x86_64__) || defined(_M_X64)
#define JMPXX_ARCH_X86_64 1
#else
#define JMPXX_ARCH_X86_64 0
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define JMPXX_ARCH_ARM64 1
#else
#define JMPXX_ARCH_ARM64 0
#endif

// 32-bit ARM. _M_ARM is the MSVC spelling; __arm__ covers GCC/Clang. AArch64
// needs no exclusion here because it defines neither, and it is matched above.
#if defined(__arm__) || defined(_M_ARM)
#define JMPXX_ARCH_ARM32 1
#else
#define JMPXX_ARCH_ARM32 0
#endif

#if defined(__riscv)
#define JMPXX_ARCH_RISCV 1
#else
#define JMPXX_ARCH_RISCV 0
#endif

#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
#define JMPXX_ARCH_WASM 1
#else
#define JMPXX_ARCH_WASM 0
#endif

#endif  // JMPXX_PLATFORM_DETECT_HPP

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

// Feature-test wrappers (guarded so an absent facility is not a hard error)
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

// Forced inlining for the thin wrappers whose only job is to vanish. Used
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

// Expression-level branch hints for the propagation fast path. The happy
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

// Diagnostic-layer switch. JMPXX_DIAGNOSTICS_ENABLED=1 turns on the debug-only
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

// Optional stack-trace capture for the diagnostic layer. JMPXX_STACKTRACE=1 makes
// a captured failure also record the return addresses of its creation site, on
// platforms where a fenced capturer is available. Off by default and meaningful
// only when JMPXX_DIAGNOSTICS_ENABLED is on. Symbolizing the addresses is an
// offline concern; the capture path takes no lock and allocates nothing. Kept off
// by default because portable, dependency-free trace capture is not universal.
#if !defined(JMPXX_STACKTRACE)
#define JMPXX_STACKTRACE 0
#endif

#endif  // JMPXX_CORE_CONFIG_HPP
// from jmpxx/core/error.hpp
// SPDX-License-Identifier: MIT
// The minimal error representation. A bare integer code plus a domain tag that
// keeps codes from different sources from colliding. It is trivially copyable
// and register-sized, carries no out-of-band context, and is usable where there
// is no heap, no exceptions, and no RTTI. Richer representations that attach
// context are layered on later without changing this one or the call sites.
#ifndef JMPXX_CORE_ERROR_HPP
#define JMPXX_CORE_ERROR_HPP


namespace jmpxx {

struct error {
  int code = 0;
  int domain = 0;

  constexpr error() noexcept = default;
  constexpr explicit error(int c, int d = 0) noexcept : code(c), domain(d) {}

  [[nodiscard]] friend constexpr bool operator==(error, error) noexcept =
      default;
};

}  // namespace jmpxx

#endif  // JMPXX_CORE_ERROR_HPP
// from jmpxx/core/propagation.hpp
// SPDX-License-Identifier: MIT
// Single-construct failure propagation, offered at two levels.
//
// Level 1, checked propagation, is the zero-overhead default. JMPXX_TRYV and
// JMPXX_TRY evaluate a result and, if it holds a failure, return that failure
// from the current function; on success JMPXX_TRY binds the produced value.
// Each adds a single predictable branch and nothing else, which is the cost a
// committed codegen golden pins. JMPXX_TRYX is the same operation usable inside
// an expression; it relies on the statement-expression extension and so is
// available on GCC and Clang only, with JMPXX_TRY as the portable form.
//
// Level 2, the landing scope, marks the single typed boundary a region's
// propagation lands at. It introduces a call boundary, which costs one frame
// unless the call is inlined, and allocates nothing; its reference entry states
// that cost.
#ifndef JMPXX_CORE_PROPAGATION_HPP
#define JMPXX_CORE_PROPAGATION_HPP

// from jmpxx/core/diagnostic_hook.hpp
// SPDX-License-Identifier: MIT
// The propagation diagnostic hook. Single-construct propagation calls
// note_propagation on the failure path so a hosted diagnostic layer can record a
// causal hop as a failure travels toward its landing boundary. The core carries the
// no-op default and jmpxx/diagnostics.hpp contributes the recording overload.
#ifndef JMPXX_CORE_DIAGNOSTIC_HOOK_HPP
#define JMPXX_CORE_DIAGNOSTIC_HOOK_HPP


namespace jmpxx {
namespace detail {

// A more specialized non-template overload, contributed by a richer policy, wins
// over this template for that policy's error type.
template <class E>
JMPXX_ALWAYS_INLINE constexpr void note_propagation(const E&) noexcept {}

}  // namespace detail
}  // namespace jmpxx

#endif  // JMPXX_CORE_DIAGNOSTIC_HOOK_HPP
// from jmpxx/core/transport.hpp
// SPDX-License-Identifier: MIT
// result<T, E>: the value-or-error transport.
//
// It holds exactly one of a value or an error and never presents a third,
// valueless state. When T and E are trivially copyable the transport is too, so
// it is returned in registers and copied by memcpy; this is the property that
// makes the happy path compile to a plain branch on a status flag. Construction,
// inspection, and extraction work in constant evaluation.
//
// The union, the discriminant, and the special members live directly in this
// class rather than in a base. A base subobject defeats GCC's return-value
// scalar replacement, which would force the transport onto the stack at a
// non-inlined boundary instead of into a register; keeping everything in one
// class preserves register return.
//
// Two access forms are offered. The checked accessors (value, error, operator*,
// operator->) verify the state and terminate on a violation, so reading a value
// from a failed result is a defined event rather than undefined behavior. The
// narrow accessors (assume_value, assume_error) state a precondition that the
// caller has already checked the state; that precondition is verified only in a
// hardened build and is otherwise the caller's contract.
#ifndef JMPXX_CORE_TRANSPORT_HPP
#define JMPXX_CORE_TRANSPORT_HPP

// from jmpxx/core/detail/utility.hpp
// SPDX-License-Identifier: MIT
// Small utilities the freestanding core needs without reaching for <utility> or
// <memory>, whose full freestanding status varies by standard version. move,
// forward, and addressof are hand-rolled here; the trait predicates that need
// compiler magic come from <type_traits>, which is freestanding and pulls in no
// hosted machinery.
#ifndef JMPXX_CORE_DETAIL_UTILITY_HPP
#define JMPXX_CORE_DETAIL_UTILITY_HPP


#include <type_traits>

namespace jmpxx {
namespace detail {

template <class T>
[[nodiscard]] JMPXX_ALWAYS_INLINE constexpr std::remove_reference_t<T>&& move(
    T&& t) noexcept {
  return static_cast<std::remove_reference_t<T>&&>(t);
}

template <class T>
[[nodiscard]] JMPXX_ALWAYS_INLINE constexpr T&& forward(
    std::remove_reference_t<T>& t) noexcept {
  return static_cast<T&&>(t);
}

template <class T>
[[nodiscard]] JMPXX_ALWAYS_INLINE constexpr T&& forward(
    std::remove_reference_t<T>&& t) noexcept {
  static_assert(!std::is_lvalue_reference_v<T>,
                "forward must not turn an rvalue into an lvalue");
  return static_cast<T&&>(t);
}

// __builtin_addressof yields the true address even for a type with an
// overloaded operator&, and is available on every supported compiler.
template <class T>
[[nodiscard]] JMPXX_ALWAYS_INLINE constexpr T* addressof(T& t) noexcept {
  return __builtin_addressof(t);
}
template <class T>
const T* addressof(const T&&) = delete;

}  // namespace detail
}  // namespace jmpxx

#endif  // JMPXX_CORE_DETAIL_UTILITY_HPP
// from jmpxx/platform/trap.hpp
// SPDX-License-Identifier: MIT
// Fenced fail-fast. Terminating the program is the one platform-specific act the
// core performs, so it lives behind this boundary rather than scattered through the
// core; docs/reference/platform.md states what a caller sees when it fires. The
// textual reason is accepted now and reserved for a hosted diagnostic sink; today
// the trap itself is the diagnostic, deterministic and visible to a debugger.
#ifndef JMPXX_PLATFORM_TRAP_HPP
#define JMPXX_PLATFORM_TRAP_HPP


namespace jmpxx {
namespace platform {

// Terminate immediately and never return. A compiler intrinsic rather than abort()
// so the core needs no hosted header and the stop is one instruction a debugger
// lands on.
[[noreturn]] JMPXX_ALWAYS_INLINE void fail_fast(const char* reason) noexcept {
  (void)reason;
#if JMPXX_HAS_BUILTIN(__builtin_trap) || JMPXX_COMPILER_GCC || JMPXX_COMPILER_CLANG
  __builtin_trap();
#elif JMPXX_COMPILER_MSVC
  __debugbreak();
#endif
  // Satisfy [[noreturn]] on any path where the intrinsic above is not itself
  // marked noreturn (for example MSVC's __debugbreak).
  for (;;) {
  }
}

}  // namespace platform
}  // namespace jmpxx

#endif  // JMPXX_PLATFORM_TRAP_HPP

#include <new>
#include <type_traits>

namespace jmpxx {

namespace detail {
// Empty stand-in for the value of a result<void, E>.
struct unit {};
}  // namespace detail

// failure<E> wraps an error so it can be returned or assigned without being
// mistaken for a value, including when the value and error types are the same.
// It converts into any result whose error type can be built from E.
template <class E>
class failure {
  E err_;

 public:
  constexpr explicit failure(const E& e) : err_(e) {}
  constexpr explicit failure(E&& e) : err_(static_cast<E&&>(e)) {}

  constexpr E& error() & noexcept { return err_; }
  constexpr const E& error() const& noexcept { return err_; }
  constexpr E&& error() && noexcept { return static_cast<E&&>(err_); }
  constexpr const E&& error() const&& noexcept {
    return static_cast<const E&&>(err_);
  }
};

template <class E>
failure(E) -> failure<E>;

// fail(e): build a failure for `return jmpxx::fail(e);`.
template <class E>
[[nodiscard]] constexpr failure<std::remove_cvref_t<E>> fail(E&& e) {
  return failure<std::remove_cvref_t<E>>(static_cast<E&&>(e));
}

struct in_place_t {
  explicit in_place_t() = default;
};
inline constexpr in_place_t in_place{};

namespace detail {
template <class>
inline constexpr bool is_failure_v = false;
template <class E>
inline constexpr bool is_failure_v<failure<E>> = true;
}  // namespace detail

template <class T, class E = error>
class JMPXX_NODISCARD(
    "a result may hold a failure; inspect it before discarding") result {
  using V = std::conditional_t<std::is_void_v<T>, detail::unit, T>;
  static constexpr bool is_void = std::is_void_v<T>;

  union {
    V val_;
    E err_;
  };
  bool has_;

  static constexpr bool triv_copy_ctor =
      std::is_trivially_copy_constructible_v<V> &&
      std::is_trivially_copy_constructible_v<E>;
  static constexpr bool triv_move_ctor =
      std::is_trivially_move_constructible_v<V> &&
      std::is_trivially_move_constructible_v<E>;
  static constexpr bool triv_dtor =
      std::is_trivially_destructible_v<V> && std::is_trivially_destructible_v<E>;
  static constexpr bool triv_copy_assign =
      triv_copy_ctor && triv_dtor && std::is_trivially_copy_assignable_v<V> &&
      std::is_trivially_copy_assignable_v<E>;
  static constexpr bool triv_move_assign =
      triv_move_ctor && triv_dtor && std::is_trivially_move_assignable_v<V> &&
      std::is_trivially_move_assignable_v<E>;
  static constexpr bool can_copy_ctor =
      std::is_copy_constructible_v<V> && std::is_copy_constructible_v<E>;
  static constexpr bool can_move_ctor =
      std::is_move_constructible_v<V> && std::is_move_constructible_v<E>;
  // Cross-state assignment reconstructs the incoming alternative in place after
  // destroying the outgoing one, so that reconstruction must not throw; both
  // alternatives are therefore required to be nothrow-move-constructible.
  static constexpr bool can_copy_assign =
      can_copy_ctor && std::is_copy_assignable_v<V> &&
      std::is_copy_assignable_v<E> && std::is_nothrow_move_constructible_v<V> &&
      std::is_nothrow_move_constructible_v<E>;
  static constexpr bool can_move_assign =
      can_move_ctor && std::is_move_assignable_v<V> &&
      std::is_move_assignable_v<E> && std::is_nothrow_move_constructible_v<V> &&
      std::is_nothrow_move_constructible_v<E>;

 public:
  using value_type = T;
  using error_type = E;

  // Value construction (non-void): from anything convertible to T, except a
  // result, a failure, or the in_place tag, which route to their own ctors.
  template <class U = T>
    requires(!is_void && std::is_constructible_v<V, U> &&
             !std::is_same_v<std::remove_cvref_t<U>, result> &&
             !std::is_same_v<std::remove_cvref_t<U>, in_place_t> &&
             !detail::is_failure_v<std::remove_cvref_t<U>>)
  constexpr result(U&& v) noexcept(std::is_nothrow_constructible_v<V, U>)
      : val_(static_cast<U&&>(v)), has_(true) {}

  // A void result's success carries no value.
  constexpr result() noexcept
    requires(is_void)
      : val_(), has_(true) {}

  // In-place value construction.
  template <class... A>
    requires(std::is_constructible_v<V, A...>)
  constexpr explicit result(in_place_t, A&&... a) noexcept(
      std::is_nothrow_constructible_v<V, A...>)
      : val_(static_cast<A&&>(a)...), has_(true) {}

  // Error construction from a failure.
  template <class E2 = E>
    requires(std::is_constructible_v<E, const E2&>)
  constexpr result(const failure<E2>& f) noexcept(
      std::is_nothrow_constructible_v<E, const E2&>)
      : err_(f.error()), has_(false) {}
  template <class E2 = E>
    requires(std::is_constructible_v<E, E2 &&>)
  constexpr result(failure<E2>&& f) noexcept(
      std::is_nothrow_constructible_v<E, E2&&>)
      : err_(static_cast<failure<E2>&&>(f).error()), has_(false) {}

  // Special members are conditionally trivial: a constrained `= default` is
  // selected when the contents allow it, a hand-written form otherwise, and a
  // deleted form when the operation is impossible.
  result(const result&)
    requires(triv_copy_ctor)
  = default;
  result(const result& o)
    requires(can_copy_ctor && !triv_copy_ctor)
      : has_(o.has_) {
    if (o.has_)
      ::new (static_cast<void*>(__builtin_addressof(val_))) V(o.val_);
    else
      ::new (static_cast<void*>(__builtin_addressof(err_))) E(o.err_);
  }
  result(const result&)
    requires(!can_copy_ctor)
  = delete;

  result(result&&)
    requires(triv_move_ctor)
  = default;
  result(result&& o)
  noexcept(std::is_nothrow_move_constructible_v<V> &&
           std::is_nothrow_move_constructible_v<E>)
    requires(can_move_ctor && !triv_move_ctor)
      : has_(o.has_) {
    if (o.has_)
      ::new (static_cast<void*>(__builtin_addressof(val_)))
          V(static_cast<V&&>(o.val_));
    else
      ::new (static_cast<void*>(__builtin_addressof(err_)))
          E(static_cast<E&&>(o.err_));
  }
  result(result&&)
    requires(!can_move_ctor)
  = delete;

  ~result()
    requires(triv_dtor)
  = default;
  constexpr ~result()
    requires(!triv_dtor)
  {
    destroy();
  }

  result& operator=(const result&)
    requires(triv_copy_assign)
  = default;
  result& operator=(const result& o)
    requires(can_copy_assign && !triv_copy_assign)
  {
    assign_from(o);
    return *this;
  }
  result& operator=(const result&)
    requires(!can_copy_assign)
  = delete;

  result& operator=(result&&)
    requires(triv_move_assign)
  = default;
  result& operator=(result&& o)
  noexcept(std::is_nothrow_move_constructible_v<V> &&
           std::is_nothrow_move_constructible_v<E> &&
           std::is_nothrow_move_assignable_v<V> &&
           std::is_nothrow_move_assignable_v<E>)
    requires(can_move_assign && !triv_move_assign)
  {
    assign_from(static_cast<result&&>(o));
    return *this;
  }
  result& operator=(result&&)
    requires(!can_move_assign)
  = delete;

  // Assigning a bare value or failure, when the transport is assignable. These
  // route through a temporary so the never-valueless assignment above is the
  // single place that handles a state transition.
  template <class U = T>
    requires(!is_void && std::is_constructible_v<V, U> &&
             std::is_move_assignable_v<result> &&
             !std::is_same_v<std::remove_cvref_t<U>, result> &&
             !detail::is_failure_v<std::remove_cvref_t<U>>)
  constexpr result& operator=(U&& v) {
    *this = result(in_place, static_cast<U&&>(v));
    return *this;
  }
  template <class E2 = E>
    requires(std::is_constructible_v<E, E2> && std::is_move_assignable_v<result>)
  constexpr result& operator=(failure<E2> f) {
    *this = result(static_cast<failure<E2>&&>(f));
    return *this;
  }

  [[nodiscard]] constexpr bool has_value() const noexcept { return has_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return has_;
  }
  [[nodiscard]] constexpr bool has_error() const noexcept { return !has_; }

  // Checked value access; defined (terminates) when holding a failure.
  constexpr decltype(auto) value() & {
    require_value();
    if constexpr (!is_void) return (val_);
  }
  constexpr decltype(auto) value() const& {
    require_value();
    if constexpr (!is_void) return (val_);
  }
  constexpr decltype(auto) value() && {
    require_value();
    if constexpr (!is_void) return detail::move(val_);
  }

  // Checked error access; defined (terminates) when holding a value.
  constexpr E& error() & {
    require_error();
    return err_;
  }
  constexpr const E& error() const& {
    require_error();
    return err_;
  }
  constexpr E&& error() && {
    require_error();
    return detail::move(err_);
  }

  // Narrow access: the precondition is that the corresponding state holds. Fast
  // hardening and above verify it; otherwise it is the caller's contract.
  constexpr decltype(auto) assume_value() & noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_value();
    if constexpr (!is_void) return (val_);
  }
  constexpr decltype(auto) assume_value() const& noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_value();
    if constexpr (!is_void) return (val_);
  }
  constexpr decltype(auto) assume_value() && noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_value();
    if constexpr (!is_void) return detail::move(val_);
  }
  constexpr E& assume_error() & noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_error();
    return err_;
  }
  constexpr const E& assume_error() const& noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_error();
    return err_;
  }
  constexpr E&& assume_error() && noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_error();
    return detail::move(err_);
  }

  // Pointer-like access to a held value (non-void), checked. The return types
  // are deduced so the members are never instantiated for result<void>.
  constexpr decltype(auto) operator->()
    requires(!is_void)
  {
    require_value();
    return detail::addressof(val_);
  }
  constexpr decltype(auto) operator->() const
    requires(!is_void)
  {
    require_value();
    return detail::addressof(val_);
  }
  constexpr decltype(auto) operator*() &
    requires(!is_void)
  {
    require_value();
    return (val_);
  }
  constexpr decltype(auto) operator*() const&
    requires(!is_void)
  {
    require_value();
    return (val_);
  }
  constexpr decltype(auto) operator*() &&
    requires(!is_void)
  {
    require_value();
    return detail::move(val_);
  }

  template <class U>
  constexpr T value_or(U&& alt) const&
    requires(!is_void && std::is_copy_constructible_v<T> &&
             std::is_convertible_v<U, T>)
  {
    return has_ ? val_ : static_cast<T>(static_cast<U&&>(alt));
  }
  template <class U>
  constexpr T value_or(U&& alt) &&
    requires(!is_void && std::is_move_constructible_v<T> &&
             std::is_convertible_v<U, T>)
  {
    return has_ ? detail::move(val_) : static_cast<T>(static_cast<U&&>(alt));
  }
  template <class G>
  constexpr E error_or(G&& alt) const& {
    return has_ ? static_cast<E>(static_cast<G&&>(alt)) : err_;
  }

  // Monadic composition, matching std::expected (P2505); the contract for each
  // combinator and for the callable it takes is in docs/reference/policies.md. The
  // call below is written out rather than routed through std::invoke because
  // <functional> is not in the freestanding subset, which is the one thing about
  // these four functions the code does not show on its own.

  // and_then: f itself returns a result; chain it on the value, propagate the failure.
  template <class F>
  constexpr auto and_then(F&& f) & {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      return has_ ? static_cast<F&&>(f)() : chained(fail(err_));
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(val_))>;
      return has_ ? static_cast<F&&>(f)(val_) : chained(fail(err_));
    }
  }
  template <class F>
  constexpr auto and_then(F&& f) const& {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      return has_ ? static_cast<F&&>(f)() : chained(fail(err_));
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(val_))>;
      return has_ ? static_cast<F&&>(f)(val_) : chained(fail(err_));
    }
  }
  template <class F>
  constexpr auto and_then(F&& f) && {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      return has_ ? static_cast<F&&>(f)() : chained(fail(detail::move(err_)));
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(detail::move(val_)))>;
      return has_ ? static_cast<F&&>(f)(detail::move(val_)) : chained(fail(detail::move(err_)));
    }
  }

  // transform: f returns a plain value; map it into a new result, propagate the failure.
  template <class F>
  constexpr auto transform(F&& f) & {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(); return result<void, E>(); }
        return result<void, E>(fail(err_));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)()) : result<chained, E>(fail(err_));
      }
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(val_))>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(val_); return result<void, E>(); }
        return result<void, E>(fail(err_));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)(val_)) : result<chained, E>(fail(err_));
      }
    }
  }
  template <class F>
  constexpr auto transform(F&& f) const& {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(); return result<void, E>(); }
        return result<void, E>(fail(err_));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)()) : result<chained, E>(fail(err_));
      }
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(val_))>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(val_); return result<void, E>(); }
        return result<void, E>(fail(err_));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)(val_)) : result<chained, E>(fail(err_));
      }
    }
  }
  template <class F>
  constexpr auto transform(F&& f) && {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(); return result<void, E>(); }
        return result<void, E>(fail(detail::move(err_)));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)())
                    : result<chained, E>(fail(detail::move(err_)));
      }
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(detail::move(val_)))>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(detail::move(val_)); return result<void, E>(); }
        return result<void, E>(fail(detail::move(err_)));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)(detail::move(val_)))
                    : result<chained, E>(fail(detail::move(err_)));
      }
    }
  }

  // or_else: f returns a result on failure; pass a value through unchanged.
  template <class F>
  constexpr auto or_else(F&& f) & {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(err_))>;
    if constexpr (is_void) return has_ ? G() : static_cast<F&&>(f)(err_);
    else return has_ ? G(in_place, val_) : static_cast<F&&>(f)(err_);
  }
  template <class F>
  constexpr auto or_else(F&& f) const& {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(err_))>;
    if constexpr (is_void) return has_ ? G() : static_cast<F&&>(f)(err_);
    else return has_ ? G(in_place, val_) : static_cast<F&&>(f)(err_);
  }
  template <class F>
  constexpr auto or_else(F&& f) && {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(detail::move(err_)))>;
    if constexpr (is_void) return has_ ? G() : static_cast<F&&>(f)(detail::move(err_));
    else return has_ ? G(in_place, detail::move(val_)) : static_cast<F&&>(f)(detail::move(err_));
  }

  // transform_error: f returns a plain new error; map the error, pass a value through.
  template <class F>
  constexpr auto transform_error(F&& f) & {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(err_))>;
    if constexpr (is_void)
      return has_ ? result<void, G>() : result<void, G>(fail(static_cast<F&&>(f)(err_)));
    else
      return has_ ? result<T, G>(in_place, val_) : result<T, G>(fail(static_cast<F&&>(f)(err_)));
  }
  template <class F>
  constexpr auto transform_error(F&& f) const& {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(err_))>;
    if constexpr (is_void)
      return has_ ? result<void, G>() : result<void, G>(fail(static_cast<F&&>(f)(err_)));
    else
      return has_ ? result<T, G>(in_place, val_) : result<T, G>(fail(static_cast<F&&>(f)(err_)));
  }
  template <class F>
  constexpr auto transform_error(F&& f) && {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(detail::move(err_)))>;
    if constexpr (is_void)
      return has_ ? result<void, G>()
                  : result<void, G>(fail(static_cast<F&&>(f)(detail::move(err_))));
    else
      return has_ ? result<T, G>(in_place, detail::move(val_))
                  : result<T, G>(fail(static_cast<F&&>(f)(detail::move(err_))));
  }

  friend constexpr bool operator==(const result& a, const result& b) {
    if (a.has_ != b.has_) return false;
    if (!a.has_) return a.err_ == b.err_;
    if constexpr (is_void)
      return true;
    else
      return a.val_ == b.val_;
  }
  template <class U>
  friend constexpr bool operator==(const result& a, const U& v)
    requires(!is_void && !detail::is_failure_v<U> &&
             !std::is_same_v<U, result>)
  {
    return a.has_ && a.val_ == v;
  }
  template <class E2>
  friend constexpr bool operator==(const result& a, const failure<E2>& f) {
    return !a.has_ && a.err_ == f.error();
  }

 private:
  constexpr void require_value() const {
    if (JMPXX_UNLIKELY(!has_))
      platform::fail_fast("jmpxx::result: value accessed on a failure");
  }
  constexpr void require_error() const {
    if (JMPXX_UNLIKELY(has_))
      platform::fail_fast("jmpxx::result: error accessed on a value");
  }
  constexpr void destroy() noexcept {
    if (has_)
      val_.~V();
    else
      err_.~E();
  }
  void assign_from(const result& o) {
    if (has_ && o.has_) {
      val_ = o.val_;
    } else if (!has_ && !o.has_) {
      err_ = o.err_;
    } else if (has_) {
      E incoming(o.err_);  // may throw; *this stays a value until it succeeds
      val_.~V();
      ::new (static_cast<void*>(__builtin_addressof(err_)))
          E(static_cast<E&&>(incoming));
      has_ = false;
    } else {
      V incoming(o.val_);
      err_.~E();
      ::new (static_cast<void*>(__builtin_addressof(val_)))
          V(static_cast<V&&>(incoming));
      has_ = true;
    }
  }
  void assign_from(result&& o) {
    if (has_ && o.has_) {
      val_ = static_cast<V&&>(o.val_);
    } else if (!has_ && !o.has_) {
      err_ = static_cast<E&&>(o.err_);
    } else if (has_) {
      E incoming(static_cast<E&&>(o.err_));
      val_.~V();
      ::new (static_cast<void*>(__builtin_addressof(err_)))
          E(static_cast<E&&>(incoming));
      has_ = false;
    } else {
      V incoming(static_cast<V&&>(o.val_));
      err_.~E();
      ::new (static_cast<void*>(__builtin_addressof(val_)))
          V(static_cast<V&&>(incoming));
      has_ = true;
    }
  }
};

}  // namespace jmpxx

#endif  // JMPXX_CORE_TRANSPORT_HPP

#include <type_traits>

#define JMPXX_DETAIL_CAT2(a, b) a##b
#define JMPXX_DETAIL_CAT(a, b) JMPXX_DETAIL_CAT2(a, b)
#define JMPXX_DETAIL_UNIQUE(base) JMPXX_DETAIL_CAT(base, __COUNTER__)

// Hop hook. On the failure path a propagation records its site with the
// diagnostic layer; for the minimal policy and in release this is nothing. It is
// a comma operand on the existing return so it adds no statement and no block,
// which keeps the diagnostics-off expansion byte-identical to a bare return and
// the zero-overhead codegen golden unchanged.
#if JMPXX_DIAGNOSTICS_ENABLED
#define JMPXX_DETAIL_HOP(err) ::jmpxx::detail::note_propagation(err),
#else
#define JMPXX_DETAIL_HOP(err)
#endif

// Propagate a failure, discarding the value on success. The enclosing function
// must return a result whose error type accepts this one.
#define JMPXX_TRYV(expr)                                               \
  do {                                                                 \
    auto&& _jmpxx_v = (expr);                                          \
    if (JMPXX_UNLIKELY(!_jmpxx_v))                                     \
      return (JMPXX_DETAIL_HOP(_jmpxx_v.assume_error())::jmpxx::fail(  \
          static_cast<decltype(_jmpxx_v)&&>(_jmpxx_v).assume_error()));\
  } while (false)

// Propagate a failure, and on success declare `auto name` bound to the value,
// moved out of the evaluated expression.
#define JMPXX_TRY(name, expr) \
  JMPXX_DETAIL_TRY(name, expr, JMPXX_DETAIL_UNIQUE(jmpxx_try_tmp_))
#define JMPXX_DETAIL_TRY(name, expr, tmp)                                  \
  auto&& tmp = (expr);                                                     \
  if (JMPXX_UNLIKELY(!tmp))                                                \
    return (JMPXX_DETAIL_HOP(tmp.assume_error())::jmpxx::fail(             \
        static_cast<decltype(tmp)&&>(tmp).assume_error()));               \
  auto name = static_cast<decltype(tmp)&&>(tmp).assume_value()

#if JMPXX_COMPILER_GCC || JMPXX_COMPILER_CLANG
// Value-yielding propagation usable inside an expression. The statement
// expression yields a freshly constructed value (a prvalue), so nothing refers
// to the evaluated temporary after it is destroyed. The statement-expression
// extension is GCC and Clang only, asked for through the platform-unit tokens so
// the platform fence stays clean.
#define JMPXX_TRYX(expr)                                                     \
  ({                                                                        \
    auto&& _jmpxx_x = (expr);                                               \
    if (JMPXX_UNLIKELY(!_jmpxx_x))                                          \
      return (JMPXX_DETAIL_HOP(_jmpxx_x.assume_error())::jmpxx::fail(       \
          static_cast<decltype(_jmpxx_x)&&>(_jmpxx_x).assume_error()));     \
    typename ::std::remove_cvref_t<decltype(_jmpxx_x)>::value_type(         \
        static_cast<decltype(_jmpxx_x)&&>(_jmpxx_x).assume_value());        \
  })
#define JMPXX_HAS_TRYX 1
#endif

namespace jmpxx {

template <class Body>
[[nodiscard]] constexpr auto try_scope(Body&& body)
    -> decltype(static_cast<Body&&>(body)()) {
  return static_cast<Body&&>(body)();
}

}  // namespace jmpxx

#endif  // JMPXX_CORE_PROPAGATION_HPP

#endif  // JMPXX_CORE_HPP
// from jmpxx/diagnostics.hpp
// SPDX-License-Identifier: MIT
// The rich default policy (rich_error) and its debug-only diagnostic layer.
//
// rich_error carries the same in-band code and domain as the minimal error; in a debug
// build it also tags the failure with a handle into a per-thread out-of-band store
// holding the origin and the causal chain, and in a release build it compiles down to
// exactly the minimal error. That release identity is the dual-personality guarantee,
// verified by the release codegen diff, not asserted here. The debug and release
// behavior, the lifetime rules, and the concurrency contract are in
// docs/reference/diagnostics.md.
//
// Hosted extension: jmpxx/core.hpp never pulls it in, and call sites select it only by
// choosing the error type. <source_location> and the origin-capturing machinery are
// named only under JMPXX_DIAGNOSTICS_ENABLED, because a source location materializes
// its file and function strings into the binary even where the value is unused.
#ifndef JMPXX_DIAGNOSTICS_HPP
#define JMPXX_DIAGNOSTICS_HPP

// The full minimal core, so the rich policy is usable from this header alone:
// result, fail, the propagation macros, the minimal error, and the hop hook the
// rich overload below specializes.

#include <cstdint>
#include <cstdio>

#if JMPXX_DIAGNOSTICS_ENABLED
// from jmpxx/diagnostic/store.hpp
// SPDX-License-Identifier: MIT
// Out-of-band diagnostic store. A failure's origin and propagation chain live beside
// the in-flight failure rather than inside the transport, so the transport stays
// narrow and intermediate frames never widen to carry context. The arena is per-thread,
// fixed-capacity, allocation-free, and initialized on first use; the exact bounds and
// overflow behavior are in docs/reference/diagnostics.md.
//
// This header is part of the debug-only diagnostic layer. It is compiled only when
// JMPXX_DIAGNOSTICS_ENABLED is on and is reached only through jmpxx/diagnostics.hpp.
#ifndef JMPXX_DIAGNOSTIC_STORE_HPP
#define JMPXX_DIAGNOSTIC_STORE_HPP


#if JMPXX_DIAGNOSTICS_ENABLED

// from jmpxx/platform/trace.hpp
// SPDX-License-Identifier: MIT
// Fenced stack-trace capture for the diagnostic layer.
//
// Capturing a return-address trace is platform-specific, so the whole mechanism
// lives behind this one boundary and nothing outside it names a platform unwinder.
// The capture records raw return addresses into a fixed buffer; it takes no lock,
// allocates nothing, and does not symbolize. Turning addresses into file and
// function names is a separate, offline concern, because symbolization is the part
// that allocates and is slow.
//
// The capability is optional and off unless JMPXX_STACKTRACE is set, and it is
// reached only through the diagnostic layer, which is itself debug-only. When the
// switch is off, capture() is a no-op that returns an empty trace and no platform
// header is included. A platform without a fenced capturer here returns an empty
// trace rather than guessing, so the absence is honest rather than silent.
#ifndef JMPXX_PLATFORM_TRACE_HPP
#define JMPXX_PLATFORM_TRACE_HPP


#include <cstddef>

#if JMPXX_STACKTRACE
// The capturer is selected here, once, by platform. glibc's execinfo backtrace is
// used on Linux; it needs frame pointers to be reliable, which a debug build keeps.
// Other platforms route to the empty capture below until a fenced capturer for
// them is added, rather than emitting an unverified trace.
#if defined(__GLIBC__) && defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define JMPXX_TRACE_BACKEND_EXECINFO 1
#endif
#endif
#endif  // JMPXX_STACKTRACE

namespace jmpxx {
namespace platform {

// A captured trace: up to max_frames return addresses, innermost first. Trivially
// copyable so a diagnostic record can hold one by value with no allocation.
struct trace {
  static constexpr int max_frames = 24;
  void* frames[max_frames];
  int count;

  [[nodiscard]] constexpr bool empty() const noexcept { return count == 0; }
};

// Capture the current call stack, skipping the innermost `skip` frames so the
// diagnostic machinery's own frames do not appear. Returns an empty trace when the
// capability is off or no fenced capturer exists for this platform. Never throws,
// never allocates, takes no lock.
[[nodiscard]] inline trace capture(int skip) noexcept {
  trace t{};
  t.count = 0;
#if defined(JMPXX_TRACE_BACKEND_EXECINFO)
  void* raw[trace::max_frames + 8];
  int want = trace::max_frames + (skip > 0 ? skip : 0);
  if (want > static_cast<int>(sizeof(raw) / sizeof(raw[0])))
    want = static_cast<int>(sizeof(raw) / sizeof(raw[0]));
  int n = ::backtrace(raw, want);
  int first = skip > 0 ? skip : 0;
  for (int i = first; i < n && t.count < trace::max_frames; ++i)
    t.frames[t.count++] = raw[i];
#else
  (void)skip;
#endif
  return t;
}

// True when a fenced capturer is compiled in, so a caller or a report can state
// plainly whether a trace is available on this build rather than implying one.
[[nodiscard]] constexpr bool trace_available() noexcept {
#if defined(JMPXX_TRACE_BACKEND_EXECINFO)
  return true;
#else
  return false;
#endif
}

}  // namespace platform
}  // namespace jmpxx

#endif  // JMPXX_PLATFORM_TRACE_HPP

#include <cstdint>
#include <source_location>

namespace jmpxx {
namespace diagnostic {

// Capacity bounds. A failure records up to max_chain propagation hops, and a thread
// holds up to max_inflight failures whose landing scope has not yet released them.
// Both are generous for ordinary use and are stated at the surface so the bound is
// honest rather than hidden; exceeding either degrades to a recorded truncation.
inline constexpr int max_chain = 16;
inline constexpr int max_inflight = 16;

namespace detail {

// One failure's out-of-band context: where it began, the sites it propagated
// through, and an optional captured trace. Trivially copyable so the arena holds it
// by value.
struct record {
  std::uint32_t id;
  std::source_location origin;
  std::source_location hops[max_chain];
  std::uint16_t hop_count;
  bool hops_truncated;
#if JMPXX_STACKTRACE
  platform::trace trace;
#endif
};

// A per-thread arena of records with a stack discipline: a record is opened when a
// failure is created in a deep frame and is released when the landing scope that
// owns it truncates the arena back to the depth it snapshotted. Records opened after
// a snapshot sit above it, so truncation reclaims exactly them.
class store {
  record records_[max_inflight];
  int depth_ = 0;
  std::uint32_t next_id_ = 1;  // 0 is reserved to mean "no record"

 public:
  // Open a record for a newly created failure and return its handle. The handle is
  // issued before the capacity check, so a failure arriving with the arena full still
  // gets one; its record is dropped and find() will not resolve it, so the failure
  // carries no context rather than corrupting another record. A handle needs to be
  // distinct only among the records live at one moment, so the counter wraps after
  // 2^32 opens and reuses values retired long before.
  std::uint32_t open(const std::source_location& origin) noexcept {
    std::uint32_t id = next_id_++;
    if (next_id_ == 0) next_id_ = 1;
    if (depth_ >= max_inflight) return id;
    record& r = records_[depth_++];
    r.id = id;
    r.origin = origin;
    r.hop_count = 0;
    r.hops_truncated = false;
#if JMPXX_STACKTRACE
    r.trace = platform::capture(/*skip=*/2);  // skip open() and the capturing ctor
#endif
    return id;
  }

  // Resolve a handle to its live record, or nullptr if it was dropped on overflow or
  // already released by its landing scope. The scan is newest-first and bounded by
  // depth_, so a released record above depth_ is never resolved.
  record* find(std::uint32_t id) noexcept {
    if (id == 0) return nullptr;
    for (int i = depth_ - 1; i >= 0; --i)
      if (records_[i].id == id) return &records_[i];
    return nullptr;
  }

  // Append a propagation hop to a failure's chain, or mark the chain truncated when
  // the bound is reached. A hop on a dropped or released handle is ignored.
  void add_hop(std::uint32_t id, const std::source_location& loc) noexcept {
    record* r = find(id);
    if (!r) return;
    if (r->hop_count >= max_chain) {
      r->hops_truncated = true;
      return;
    }
    r->hops[r->hop_count++] = loc;
  }

  // A landing scope snapshots this on entry.
  [[nodiscard]] int mark() const noexcept { return depth_; }

  // Release every record opened since a snapshot, bounding diagnostic lifetime to
  // the landing scope that took it. A snapshot at or above the current depth is a
  // no-op, so an out-of-order or stale mark cannot grow the arena.
  void truncate(int m) noexcept {
    if (m >= 0 && m < depth_) depth_ = m;
  }
};

// One arena per thread per program. The function-local thread_local is initialized
// on first use, so a failure produced before main initializes the arena on the
// thread that produced it without any static-initialization-order dependency.
inline store& tls() noexcept {
  static thread_local store s;
  return s;
}

}  // namespace detail
}  // namespace diagnostic
}  // namespace jmpxx

#endif  // JMPXX_DIAGNOSTICS_ENABLED
#endif  // JMPXX_DIAGNOSTIC_STORE_HPP
#include <source_location>
#endif

namespace jmpxx {

// The rich default error representation. Trivially copyable in every build so the
// transport that carries it stays register-passable and allocation-free. In debug
// it adds one handle word that ties the failure to its out-of-band context; in
// release it is exactly the minimal error.
//
// Failure modes and concurrency: a rich_error never throws and never allocates. Its
// origin and chain live in the thread-local store, so a rich_error read from the
// thread that produced it resolves its context, while one moved to another thread
// resolves to no context rather than to another thread's, which is what keeps the
// layer race-free. Reading the context is valid only while the owning landing scope
// is alive; see landing and diagnostic::inspect.
class rich_error {
 public:
  // The in-band payload, named and laid out exactly as the minimal error, so a
  // landing that reads err.code and err.domain reads the same source under either
  // policy. The field names keep one source usable under every shipped policy.
  int code = 0;
  int domain = 0;

 private:
#if JMPXX_DIAGNOSTICS_ENABLED
  std::uint32_t origin_ = 0;  // handle into the thread-local store; 0 == none
#endif

 public:
  constexpr rich_error() noexcept = default;
  rich_error(const rich_error&) noexcept = default;
  rich_error(rich_error&&) noexcept = default;
  rich_error& operator=(const rich_error&) noexcept = default;
  rich_error& operator=(rich_error&&) noexcept = default;

#if JMPXX_DIAGNOSTICS_ENABLED
  // The location defaults to the construction site, so a failure site that writes
  // rich_error(code, domain) captures where it failed without naming a location.
  // These touch the thread-local store, so they are not constexpr; the constexpr
  // surface is the release representation below.
  explicit rich_error(
      int c, int d = 0,
      std::source_location loc = std::source_location::current()) noexcept
      : code(c), domain(d), origin_(diagnostic::detail::tls().open(loc)) {}
  rich_error(error e,
             std::source_location loc = std::source_location::current()) noexcept
      : code(e.code), domain(e.domain), origin_(diagnostic::detail::tls().open(loc)) {}

  // The handle the propagation hook uses to attach a hop. Not part of the stable
  // surface; it exists only in debug.
  [[nodiscard]] std::uint32_t handle() const noexcept { return origin_; }
#else
  constexpr explicit rich_error(int c, int d = 0) noexcept : code(c), domain(d) {}
  constexpr rich_error(error e) noexcept : code(e.code), domain(e.domain) {}
#endif

  // Decay to the minimal error, carrying the in-band code and domain. Explicit so a
  // rich_error is never silently narrowed where the rich type was intended.
  [[nodiscard]] constexpr error base() const noexcept { return error(code, domain); }
  [[nodiscard]] constexpr explicit operator error() const noexcept {
    return error(code, domain);
  }

  // Equal when the in-band code and domain match. The out-of-band context is
  // per-occurrence and is not part of value identity.
  [[nodiscard]] friend constexpr bool operator==(rich_error a,
                                                  rich_error b) noexcept {
    return a.code == b.code && a.domain == b.domain;
  }
};

static_assert(__is_trivially_copyable(rich_error),
              "rich_error must stay trivially copyable so the transport that "
              "carries it stays register-passable; the diagnostic context is held "
              "out of band, never in the error");

// A landing scope owns the diagnostic context of every failure created under it.
// The lifetime contract a caller needs is in docs/reference/diagnostics.md; what the
// code cannot show is why the bound is a store mark rather than a count of records:
// releasing to a mark makes nesting free and makes an early return release exactly
// what the scope added, with no bookkeeping per failure.
class landing {
#if JMPXX_DIAGNOSTICS_ENABLED
  int mark_;

 public:
  landing() noexcept : mark_(diagnostic::detail::tls().mark()) {}
  ~landing() { diagnostic::detail::tls().truncate(mark_); }
#else
 public:
  constexpr landing() noexcept = default;
#endif
  landing(const landing&) = delete;
  landing& operator=(const landing&) = delete;
};

namespace detail {
#if JMPXX_DIAGNOSTICS_ENABLED
// The rich-policy overload of the propagation hook. The single-construct
// propagation macros call note_propagation on the failure path; for rich_error this
// records the propagation site as a hop, capturing the call-site location through
// the defaulted argument. Selected over the core's no-op template by overload
// resolution, and present only in debug, so the minimal policy and every release
// build record nothing.
JMPXX_ALWAYS_INLINE void note_propagation(
    const rich_error& e,
    std::source_location loc = std::source_location::current()) noexcept {
  diagnostic::detail::tls().add_hop(e.handle(), loc);
}
#endif
}  // namespace detail

namespace diagnostic {

#if JMPXX_DIAGNOSTICS_ENABLED
// A read-only view of a failure's captured context. The validity window, the meaning
// of each field, and what a caller may not retain are in
// docs/reference/diagnostics.md. The view is a borrowed pointer rather than a copy so
// that inspecting a failure allocates nothing and copies no chain.
struct context {
  bool available;
  std::source_location origin;
  const std::source_location* hops;
  int hop_count;
  bool hops_truncated;
  platform::trace trace;
  bool trace_captured;
};

// Resolve a rich error's out-of-band context on the calling thread.
[[nodiscard]] inline context inspect(const rich_error& e) noexcept {
  context c{};
  detail::record* r = detail::tls().find(e.handle());
  if (!r) {
    c.available = false;
    return c;
  }
  c.available = true;
  c.origin = r->origin;
  c.hops = r->hops;
  c.hop_count = r->hop_count;
  c.hops_truncated = r->hops_truncated;
#if JMPXX_STACKTRACE
  c.trace = r->trace;
  c.trace_captured = !r->trace.empty();
#else
  c.trace = {};
  c.trace_captured = false;
#endif
  return c;
}

// The number of propagation hops recorded for a failure, or 0 if none.
[[nodiscard]] inline int chain_length(const rich_error& e) noexcept {
  detail::record* r = detail::tls().find(e.handle());
  return r ? r->hop_count : 0;
}
#endif  // JMPXX_DIAGNOSTICS_ENABLED

// Write a failure's origin and causal chain to out, one frame per line. In debug it
// renders the captured context; in release it is a no-op, because the context does
// not exist. A program calls this the same way in both builds, and the release call
// compiles to nothing.
inline void print(const rich_error& e, std::FILE* out) noexcept {
#if JMPXX_DIAGNOSTICS_ENABLED
  context c = inspect(e);
  if (!c.available) {
    std::fprintf(out, "  (no diagnostic context: code=%d domain=%d)\n", e.code,
                 e.domain);
    return;
  }
  std::fprintf(out, "  origin: %s:%u  %s\n", c.origin.file_name(),
               c.origin.line(), c.origin.function_name());
  for (int i = 0; i < c.hop_count; ++i)
    std::fprintf(out, "  via:    %s:%u  %s\n", c.hops[i].file_name(),
                 c.hops[i].line(), c.hops[i].function_name());
  if (c.hops_truncated) std::fprintf(out, "  ... (chain truncated)\n");
  if (c.trace_captured)
    std::fprintf(out, "  trace:  %d raw frame%s\n", c.trace.count,
                 c.trace.count == 1 ? "" : "s");
#else
  (void)e;
  (void)out;
#endif
}

}  // namespace diagnostic
}  // namespace jmpxx

#endif  // JMPXX_DIAGNOSTICS_HPP
// from jmpxx/erased.hpp
// SPDX-License-Identifier: MIT
// The type-erased boundary policy.
//
// erased_error carries a domain-tagged error that code on the far side of a
// component boundary can inspect without knowing the originating error category;
// docs/reference/policies.md states what that boundary reads and what identity it
// can rely on. The representation is two scalars and a static descriptor because a
// boundary error must still travel in registers and allocate nothing, and the
// descriptor's virtual dispatch is what carries type identity without RTTI, since
// only typeid and dynamic_cast need it.
//
// Hosted extension. The minimal core never pulls it in, but the header follows the
// core's freestanding-friendly discipline and includes no hosted header, so a
// freestanding boundary can still erase errors when it selects this policy explicitly.
#ifndef JMPXX_ERASED_HPP
#define JMPXX_ERASED_HPP

// The full minimal core, so the type-erased policy is usable from this header alone:
// result and the transport the policy is selected over, plus the minimal error. The
// core is freestanding, so pulling it in keeps this header freestanding-friendly.

namespace jmpxx {

// A descriptor for one family of error values: how the family is named and how a
// value within it renders to text. One static instance exists per family, and an
// erased_error points at it, so the descriptor's address is the family's identity.
//
// Concurrency: a domain is immutable after construction and its methods are
// const, so a single static instance is shared across threads without
// synchronization. Lifetime: domains have static storage duration and are never
// destroyed through this base, so the protected destructor forbids
// `delete base_ptr`. message() returns a string with static storage duration so
// it allocates nothing and the caller need not free it.
class error_domain {
 public:
  constexpr error_domain() noexcept = default;
  error_domain(const error_domain&) = delete;
  error_domain& operator=(const error_domain&) = delete;

  // A short, stable identifier for the family, for example "jmpxx.generic".
  [[nodiscard]] virtual const char* name() const noexcept = 0;

  // A human-readable rendering of one value in this family, with static storage
  // duration. A domain that has no per-value text returns a family-level string.
  [[nodiscard]] virtual const char* message(int value) const noexcept = 0;

 protected:
  ~error_domain() = default;
};

namespace detail {

[[nodiscard]] constexpr int fold_generic_error(int code, int domain_tag) noexcept {
  return static_cast<int>(static_cast<unsigned>(code) ^
                          (static_cast<unsigned>(domain_tag) << 16));
}

// The built-in domain for errors that carry only a bare code, so the same
// call-site source that constructs a minimal error constructs an erased one. It
// names itself and renders every value with one family-level string, because a
// bare code carries no per-value meaning the library can name.
class generic_error_domain final : public error_domain {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "jmpxx.generic"; }
  [[nodiscard]] const char* message(int) const noexcept override {
    return "unspecified error";
  }
};

inline constexpr generic_error_domain generic_domain_instance{};

}  // namespace detail

// The generic domain singleton. Its address is a constant expression, so an
// erased_error built from a bare code is usable in constant evaluation.
[[nodiscard]] constexpr const error_domain& generic_domain() noexcept {
  return detail::generic_domain_instance;
}

// A type-erased, domain-tagged error; the boundary contract is in
// docs/reference/policies.md. The value is held beside the descriptor rather than
// inside it so that two errors from one domain differ without a second descriptor.
class erased_error {
  int value_ = 0;
  const error_domain* domain_ = &generic_domain();

  [[nodiscard]] constexpr const error_domain* checked_domain() const noexcept {
    if constexpr (JMPXX_HARDENING_EXTENSIVE_ENABLED) {
      if (JMPXX_UNLIKELY(domain_ == nullptr))
        platform::fail_fast("jmpxx::erased_error: null error domain");
    }
    return domain_;
  }

 public:
  constexpr erased_error() noexcept = default;

  // Policy-uniform construction from a bare code, matching the minimal error's
  // shape so identical call-site source serves both policies. The second argument
  // is the minimal error's coarse domain tag, folded into the value by unsigned
  // arithmetic over the low bits of the two integers. The fold round-trips a code
  // and a tag that each fit in 16 bits, which covers the common small-code case at
  // no extra storage, but it is coarse, not lossless in general: a code that uses
  // the high 16 bits can collide with a tagged code, for example erased_error(0, 1)
  // and erased_error(65536, 0) both hold 65536. Code that must carry a full-width
  // code and a distinct domain across a boundary names a domain descriptor through
  // the (value, error_domain&) constructor below instead.
  constexpr explicit erased_error(int code, int domain_tag = 0) noexcept
      : value_(detail::fold_generic_error(code, domain_tag)),
        domain_(&generic_domain()) {}

  // Boundary construction: a component erases its own error into the uniform form
  // by naming its domain. The value is the component's own code within that
  // domain.
  constexpr erased_error(int value, const error_domain& dom) noexcept
      : value_(value), domain_(&dom) {}

  // Erase a minimal error into a named domain, preserving its code as the value.
  constexpr erased_error(error e, const error_domain& dom) noexcept
      : value_(e.code), domain_(&dom) {}

  [[nodiscard]] constexpr int value() const noexcept { return value_; }
  [[nodiscard]] constexpr const error_domain& domain() const noexcept {
    return *checked_domain();
  }

  // Same-domain membership by descriptor identity, without RTTI.
  [[nodiscard]] constexpr bool in_domain(const error_domain& dom) const noexcept {
    return checked_domain() == &dom;
  }

  [[nodiscard]] const char* domain_name() const noexcept {
    return checked_domain()->name();
  }
  [[nodiscard]] const char* message() const noexcept {
    return checked_domain()->message(value_);
  }

  // Two erased errors are equal when they name the same domain and the same value.
  [[nodiscard]] friend constexpr bool operator==(erased_error a,
                                                  erased_error b) noexcept {
    return a.domain_ == b.domain_ && a.value_ == b.value_;
  }
};

static_assert(__is_trivially_copyable(erased_error),
              "erased_error must stay trivially copyable so the transport that "
              "carries it stays register-passable and allocation-free");

}  // namespace jmpxx

#endif  // JMPXX_ERASED_HPP
// from jmpxx/reflect.hpp
// SPDX-License-Identifier: MIT
// The optional reflection forward layer.
//
// Where C++26 static reflection (P2996) is available, this layer derives error
// metadata that a program would otherwise hand-maintain: an enumerator's text from
// its value and back, an error_domain whose per-value message is the enumerator name
// rather than a hand-written switch, and the static set of failures an error enum
// declares. Where reflection is absent, every one of those is delivered by a
// hand-written C++20 equivalent that parses the compiler's function signature, so the
// surface and the results are identical and a program written against this layer
// builds and behaves the same on a C++20 toolchain and a reflection-capable one.
//
// Nothing in the core requires this layer. It is reached only by including this header,
// depends only on the error representations it derives metadata for, and leaves the
// full core buildable on a C++20 toolchain with this layer absent.
// The reflection path is selected by JMPXX_REFLECTION, which defaults to the standard
// __cpp_lib_reflection feature test and can be forced on for a pre-standardization
// toolchain that implements P2996 without yet advertising the macro.
//
// The two paths are guaranteed to agree on the metadata they derive, which the
// behavioral tier checks by running both and comparing. Where they differ is the
// range of enumerator values the fallback can see, which docs/reference/reflect.md
// states along with the trait that widens it.
#ifndef JMPXX_REFLECT_HPP
#define JMPXX_REFLECT_HPP


#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

// JMPXX_REFLECTION is 1 when C++26 static reflection is available. The default is the
// standard library feature-test macro; a toolchain that implements P2996 before
// defining it (a pre-standardization fork) is opted in with -DJMPXX_REFLECTION=1.
#if !defined(JMPXX_REFLECTION)
#if defined(__cpp_lib_reflection) && __cpp_lib_reflection >= 202506L
#define JMPXX_REFLECTION 1
#else
#define JMPXX_REFLECTION 0
#endif
#endif

#if JMPXX_REFLECTION
#include <meta>
#endif

namespace jmpxx {
namespace reflect {

// One failure an error enum declares: its underlying value and its enumerator name.
// The name aliases static storage that outlives the program, so a failure_info is
// freely copied and stored.
struct failure_info {
  int value;
  std::string_view name;
};

// The value range the C++20 fallback scans for enumerators. Specialize for an enum
// whose enumerators fall outside the default window. Unused on the reflection path,
// which enumerates directly.
template <class E>
struct enum_range {
  static constexpr int min = -128;
  static constexpr int max = 127;
};

namespace detail {

#if !JMPXX_REFLECTION

// Recover an enumerator's spelling by instantiating this template on the enum
// constant and slicing the compiler's pretty signature. The markers bracket the
// value so that GCC appending the return-type alias, with its own '=', does not fool
// the slice: GCC and Clang read after "V = " and stop at the first ']' or ';'; MSVC
// reads between the function-name '<' and ">(". The MSVC vs GCC/Clang spelling choice
// asks the platform unit through JMPXX_COMPILER_MSVC rather than reading a raw
// compiler macro, so the platform fence stays clean. __PRETTY_FUNCTION__ and
// __FUNCSIG__ themselves are compiler-portability builtins, not platform constructs.
template <auto V>
constexpr std::string_view enumerator_spelling() noexcept {
#if JMPXX_COMPILER_MSVC
  std::string_view p = __FUNCSIG__;
  const auto end = p.rfind(">(");
  const auto lt = p.rfind('<', end);
  p = p.substr(lt + 1, end - lt - 1);
  if (p.rfind("enum ", 0) == 0) p.remove_prefix(5);
#else
  std::string_view p = __PRETTY_FUNCTION__;
  const auto m = p.find("V = ");
  p.remove_prefix(m + 4);
  p = p.substr(0, p.find_first_of(";]"));
#endif
  const auto scope = p.rfind("::");
  if (scope != std::string_view::npos) p.remove_prefix(scope + 2);
  return p;
}

// True when V names an enumerator. A value that is not an enumerator prints as a cast,
// "(app::status)99", "status(99)", or a bare number; after scope stripping that leaves
// a fragment like "status)99" or a leading digit, none of which is a bare identifier.
// Requiring every character of the stripped spelling to be an identifier character,
// and the first not to be a digit, rejects all those forms and accepts only a real
// enumerator name.
template <auto V>
constexpr bool is_enumerator() noexcept {
  const std::string_view n = enumerator_spelling<V>();
  if (n.empty()) return false;
  const char f = n.front();
  if (!(f == '_' || (f >= 'a' && f <= 'z') || (f >= 'A' && f <= 'Z'))) return false;
  for (char c : n)
    if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9')))
      return false;
  return true;
}

// The unqualified spelling of a type, sliced the same way, so the fallback's type name
// matches the reflection path's identifier_of, which is also unqualified.
template <class T>
constexpr std::string_view type_spelling() noexcept {
#if JMPXX_COMPILER_MSVC
  std::string_view p = __FUNCSIG__;
  const auto end = p.rfind(">(");
  const auto lt = p.rfind('<', end);
  p = p.substr(lt + 1, end - lt - 1);
  if (p.rfind("enum ", 0) == 0) p.remove_prefix(5);
#else
  std::string_view p = __PRETTY_FUNCTION__;
  const auto m = p.find("T = ");
  p.remove_prefix(m + 4);
  p = p.substr(0, p.find_first_of(";]"));
#endif
  const auto scope = p.rfind("::");
  if (scope != std::string_view::npos) p.remove_prefix(scope + 2);
  return p;
}

template <class E>
constexpr int range_lo() noexcept {
  return enum_range<E>::min;
}
template <class E>
constexpr int range_span() noexcept {
  return enum_range<E>::max - enum_range<E>::min + 1;
}

template <class E>
constexpr std::size_t count_enumerators() noexcept {
  return [&]<int... I>(std::integer_sequence<int, I...>) {
    return (std::size_t{0} + ... +
            (is_enumerator<static_cast<E>(range_lo<E>() + I)>() ? 1u : 0u));
  }(std::make_integer_sequence<int, range_span<E>()>());
}

template <class E>
constexpr std::string_view scan_name(E v) noexcept {
  return [&]<int... I>(std::integer_sequence<int, I...>) {
    std::string_view out;
    (void)(((static_cast<int>(v) == range_lo<E>() + I &&
             is_enumerator<static_cast<E>(range_lo<E>() + I)>())
                ? (out = enumerator_spelling<static_cast<E>(range_lo<E>() + I)>(),
                   true)
                : false) ||
           ...);
    return out;
  }(std::make_integer_sequence<int, range_span<E>()>());
}

template <class E>
constexpr std::optional<E> scan_value(std::string_view name) noexcept {
  return [&]<int... I>(std::integer_sequence<int, I...>) {
    std::optional<E> out;
    (void)(((is_enumerator<static_cast<E>(range_lo<E>() + I)>() &&
             enumerator_spelling<static_cast<E>(range_lo<E>() + I)>() == name)
                ? (out = static_cast<E>(range_lo<E>() + I), true)
                : false) ||
           ...);
    return out;
  }(std::make_integer_sequence<int, range_span<E>()>());
}

// The static failure set, value-ordered. The scan visits values in ascending order,
// so collecting in scan order is already value-ordered, which matches the order the
// reflection path sorts into, so failures<E>() is identical on both paths.
template <class E>
constexpr auto build_failures() noexcept {
  std::array<failure_info, count_enumerators<E>()> out{};
  std::size_t k = 0;
  [&]<int... I>(std::integer_sequence<int, I...>) {
    (void)((is_enumerator<static_cast<E>(range_lo<E>() + I)>()
                ? (out[k++] = failure_info{range_lo<E>() + I,
                                           enumerator_spelling<static_cast<E>(
                                               range_lo<E>() + I)>()},
                   void())
                : void()),
           ...);
  }(std::make_integer_sequence<int, range_span<E>()>());
  return out;
}

// A null-terminated static copy of one enumerator's name, for the domain message
// surface, which is a C string. The view from the signature is not terminated at the
// enumerator boundary, so it is copied once into static storage here.
template <auto V>
struct enumerator_cstr {
  static constexpr auto storage = []() noexcept {
    constexpr std::string_view sv = enumerator_spelling<V>();
    std::array<char, sv.size() + 1> a{};
    for (std::size_t i = 0; i < sv.size(); ++i) a[i] = sv[i];
    a[sv.size()] = '\0';
    return a;
  }();
};
template <auto V>
constexpr const char* enumerator_c_str() noexcept {
  // if constexpr keeps the null-terminated copy from being instantiated for the
  // unnamed values the scan also visits, so only real enumerators cost storage.
  if constexpr (is_enumerator<V>())
    return enumerator_cstr<V>::storage.data();
  else
    return nullptr;
}

template <class T>
struct type_cstr {
  static constexpr auto storage = []() noexcept {
    constexpr std::string_view sv = type_spelling<T>();
    std::array<char, sv.size() + 1> a{};
    for (std::size_t i = 0; i < sv.size(); ++i) a[i] = sv[i];
    a[sv.size()] = '\0';
    return a;
  }();
};

// A value-indexed table of null-terminated enumerator names for the domain's runtime
// message lookup: slot v - min is the name when v is an enumerator and null otherwise.
template <class E>
inline constexpr auto name_table = []() noexcept {
  std::array<const char*, range_span<E>()> t{};
  [&]<int... I>(std::integer_sequence<int, I...>) {
    ((t[I] = enumerator_c_str<static_cast<E>(range_lo<E>() + I)>()), ...);
  }(std::make_integer_sequence<int, range_span<E>()>());
  return t;
}();

#endif  // !JMPXX_REFLECTION

}  // namespace detail

#if JMPXX_REFLECTION

// The enumerator name for a value, or an empty view when the value names no
// enumerator. Constant-evaluable and usable at run time; the reflection work is
// confined to the compile-time loop and the returned view aliases static metadata.
template <class E>
  requires std::is_enum_v<E>
constexpr std::string_view enum_name(E value) noexcept {
  template for (constexpr auto e :
                std::define_static_array(std::meta::enumerators_of(^^E)))
    if (value == [:e:]) return std::meta::identifier_of(e);
  return {};
}

// The enumerator a name denotes, or no value when the name matches none.
template <class E>
  requires std::is_enum_v<E>
constexpr std::optional<E> enum_cast(std::string_view name) noexcept {
  template for (constexpr auto e :
                std::define_static_array(std::meta::enumerators_of(^^E)))
    if (name == std::meta::identifier_of(e)) return [:e:];
  return std::nullopt;
}

// The unqualified name of the enum type.
template <class E>
  requires std::is_enum_v<E>
constexpr std::string_view type_name() noexcept {
  return std::meta::identifier_of(^^E);
}

// The number of enumerators the type declares.
template <class E>
  requires std::is_enum_v<E>
constexpr std::size_t enum_count() noexcept {
  return std::meta::enumerators_of(^^E).size();
}

namespace detail {
// The static, value-ordered failure set. Each name is promoted to static storage with
// define_static_string so the {value, name} pairs survive to run time. The result is a
// plain std::array, not a define_static_array, because failure_info holds a
// string_view, which is not a structural type a reflection can promote element by
// element. Insertion keeps it value-ordered, matching the fallback's scan order.
template <class E>
constexpr auto build_failures() {
  std::array<failure_info, reflect::enum_count<E>()> out{};
  std::size_t k = 0;
  template for (constexpr auto e :
                std::define_static_array(std::meta::enumerators_of(^^E))) {
    failure_info fi{static_cast<int>([:e:]),
                    std::string_view(
                        std::define_static_string(std::meta::identifier_of(e)))};
    std::size_t j = k;
    while (j > 0 && out[j - 1].value > fi.value) {
      out[j] = out[j - 1];
      --j;
    }
    out[j] = fi;
    ++k;
  }
  return out;
}
template <class E>
inline constexpr auto failures_storage = build_failures<E>();
}  // namespace detail

#else  // C++20 hand-written fallback

template <class E>
  requires std::is_enum_v<E>
constexpr std::string_view enum_name(E value) noexcept {
  return detail::scan_name<E>(value);
}

template <class E>
  requires std::is_enum_v<E>
constexpr std::optional<E> enum_cast(std::string_view name) noexcept {
  return detail::scan_value<E>(name);
}

template <class E>
  requires std::is_enum_v<E>
constexpr std::string_view type_name() noexcept {
  return detail::type_spelling<E>();
}

template <class E>
  requires std::is_enum_v<E>
constexpr std::size_t enum_count() noexcept {
  return detail::count_enumerators<E>();
}

namespace detail {
template <class E>
inline constexpr auto failures_storage = build_failures<E>();
}  // namespace detail

#endif  // JMPXX_REFLECTION

// The static set of failures the enum declares, value-ordered, as {value, name}
// pairs. For an error enum this is the statically derivable specification of what a
// unit reporting that enum can fail with. Identical on both paths within the
// fallback's scanned range.
template <class E>
  requires std::is_enum_v<E>
constexpr std::span<const failure_info> failures() noexcept {
  return {detail::failures_storage<E>.data(), detail::failures_storage<E>.size()};
}

namespace detail {

// An error_domain whose identity is an enum type and whose per-value message is the
// derived enumerator name, so a component reports type-erased errors keyed by its enum
// with no hand-written name table. One instance exists per enum type.
template <class E>
class reflected_domain final : public error_domain {
 public:
  [[nodiscard]] const char* name() const noexcept override {
#if JMPXX_REFLECTION
    return std::define_static_string(reflect::type_name<E>());
#else
    return type_cstr<E>::storage.data();
#endif
  }
  [[nodiscard]] const char* message(int value) const noexcept override {
#if JMPXX_REFLECTION
    const char* out = "unknown error";
    template for (constexpr auto e :
                  std::define_static_array(std::meta::enumerators_of(^^E)))
      if (value == static_cast<int>([:e:]))
        out = std::define_static_string(std::meta::identifier_of(e));
    return out;
#else
    const int lo = range_lo<E>();
    if (value >= lo && value < lo + range_span<E>()) {
      const char* s = name_table<E>[value - lo];
      if (s) return s;
    }
    return "unknown error";
#endif
  }
};

template <class E>
inline const reflected_domain<E> reflected_domain_instance{};

}  // namespace detail

// The error_domain derived from an enum type. Its name is the enum's type name and its
// message for a value is that value's enumerator name, both derived rather than
// hand-written. A component selects it once and reports erased errors keyed by its own
// enum without writing a name switch.
template <class E>
  requires std::is_enum_v<E>
[[nodiscard]] const error_domain& enum_domain() noexcept {
  return detail::reflected_domain_instance<E>;
}

// Build a type-erased error from an enumerator, tagged with the enum's derived domain.
// The boundary that reads it gets the enumerator name as the message with no table the
// component had to maintain.
template <class E>
  requires std::is_enum_v<E>
[[nodiscard]] erased_error as_erased(E value) noexcept {
  return erased_error(static_cast<int>(value), enum_domain<E>());
}

}  // namespace reflect
}  // namespace jmpxx

#endif  // JMPXX_REFLECT_HPP
// from jmpxx/platform.hpp
// SPDX-License-Identifier: MIT
// Platform abstraction: the library's single boundary for target-specific and
// ABI-specific facts and constructs. Detection lives in platform/detect.hpp, the
// fenced fail-fast in platform/trap.hpp, and the optional trace capture in
// platform/trace.hpp; this umbrella exposes the small caller-facing target queries.
// Every platform-specific construct in the library resides under this unit (or the
// later, separately fenced unwind arm), which the platform-fence scan enforces.
//
// It pulls in only the freestanding detection and trap headers, so including it
// adds no hosted dependency. The optional trace capture is not pulled in here
// because it is heavier and off by default; a consumer that wants it includes
// platform/trace.hpp directly.
#ifndef JMPXX_PLATFORM_HPP
#define JMPXX_PLATFORM_HPP


namespace jmpxx {
namespace platform {

// The compiler that built this translation unit, as a short stable identifier.
// One of "gcc", "clang", "msvc", or "unknown".
[[nodiscard]] constexpr const char* compiler_name() noexcept {
#if JMPXX_COMPILER_CLANG
  return "clang";
#elif JMPXX_COMPILER_GCC
  return "gcc";
#elif JMPXX_COMPILER_MSVC
  return "msvc";
#else
  return "unknown";
#endif
}

// The operating system this translation unit targets. "none" is a freestanding
// or bare-metal target with no hosted OS.
[[nodiscard]] constexpr const char* os_name() noexcept {
#if JMPXX_OS_WINDOWS
  return "windows";
#elif JMPXX_OS_MACOS
  return "macos";
#elif JMPXX_OS_LINUX
  return "linux";
#elif JMPXX_OS_BSD
  return "bsd";
#else
  return "none";
#endif
}

// The target architecture. "other" is a target outside the supported matrix,
// reported honestly rather than guessed.
[[nodiscard]] constexpr const char* arch_name() noexcept {
#if JMPXX_ARCH_X86_64
  return "x86_64";
#elif JMPXX_ARCH_ARM64
  return "arm64";
#elif JMPXX_ARCH_ARM32
  return "arm32";
#elif JMPXX_ARCH_RISCV
  return "riscv";
#elif JMPXX_ARCH_WASM
  return "wasm";
#else
  return "other";
#endif
}

// Whether a hosted operating system is present. False on a freestanding or
// bare-metal target, where the minimal core is the supported surface.
[[nodiscard]] constexpr bool is_hosted() noexcept { return JMPXX_OS_HOSTED != 0; }

}  // namespace platform
}  // namespace jmpxx

#endif  // JMPXX_PLATFORM_HPP
// from jmpxx/interop.hpp
// SPDX-License-Identifier: MIT
// The interoperability bridges, gathered for a hosted consumer.
//
// Including this umbrella is a hosted choice: the error_code bridge needs
// <system_error> and the expected bridge needs <expected>.
#ifndef JMPXX_INTEROP_HPP
#define JMPXX_INTEROP_HPP

// from jmpxx/interop/adapt.hpp
// SPDX-License-Identifier: MIT
// Small adapters that turn the optional-like and boolean-plus-value results of
// other libraries into a jmpxx result at a boundary.
//
// A library that reports absence with an optional-like value, std::optional, a raw
// pointer, or a toml/json node accessor, leaves a caller writing the same shape at
// every boundary: if it is empty, fail with some error, otherwise take the value.
// from_optional collapses that into one call, supplying the failure only when the
// value is absent. from_condition is the more general form for a library that
// reports success as a separate boolean and produces its value through a call.
//
// These adapters duck-type their argument: they require only a contextual
// conversion to bool and operator*, never a specific standard header, so the
// header pulls in nothing outside the freestanding subset and is usable in the
// minimal core's configuration. It is still interop rather than core, selected by
// a consumer that has an external value to adapt, so it lives here and is not
// pulled in by jmpxx/core.hpp.
#ifndef JMPXX_INTEROP_ADAPT_HPP
#define JMPXX_INTEROP_ADAPT_HPP


#include <type_traits>

namespace jmpxx {

// Adapt an optional-like value into a result. `o` is contextually converted to
// bool to test for a value and dereferenced with operator* to read it. When it
// holds a value the result holds that value; when it is empty the result holds the
// failure on_empty produces. The value type is whatever operator* yields, so an
// engaged std::optional<T> becomes result<T, E> and a non-null pointer becomes a
// result over its pointee.
template <class E, class Opt, class MakeError>
[[nodiscard]] constexpr auto from_optional(Opt&& o, MakeError&& on_empty)
    -> result<std::remove_cvref_t<decltype(*static_cast<Opt&&>(o))>, E> {
  using T = std::remove_cvref_t<decltype(*static_cast<Opt&&>(o))>;
  if (o) return result<T, E>(in_place, *static_cast<Opt&&>(o));
  return result<T, E>(fail(static_cast<MakeError&&>(on_empty)()));
}

// Adapt a boolean-plus-value result into a result. `present` says whether a value
// is available; when true the value comes from take(), and when false the failure
// comes from on_absent(). This fits a library whose success flag and value are
// separate, such as a parse result that is tested for success and then asked for
// its parsed object, where there is no single optional to dereference.
template <class E, class Take, class MakeError>
[[nodiscard]] constexpr auto from_condition(bool present, Take&& take,
                                            MakeError&& on_absent)
    -> result<std::remove_cvref_t<decltype(static_cast<Take&&>(take)())>, E> {
  using T = std::remove_cvref_t<decltype(static_cast<Take&&>(take)())>;
  if (present) return result<T, E>(in_place, static_cast<Take&&>(take)());
  return result<T, E>(fail(static_cast<MakeError&&>(on_absent)()));
}

}  // namespace jmpxx

#endif  // JMPXX_INTEROP_ADAPT_HPP
// from jmpxx/interop/error_code.hpp
// SPDX-License-Identifier: MIT
// Bridge between jmpxx errors and the standard <system_error> facility,
// std::error_code. Two directions: a foreign std::error_code is carried verbatim into a
// result<T, std::error_code> (the transport is generic over its error type, so the
// category and value are preserved exactly), and a jmpxx::error is exposed as a
// std::error_code in a jmpxx-owned category that round-trips losslessly through
// to_error_code / make_error_code / from_error_code. The directions and their fidelity,
// including the lossy case of recovering a foreign category, are in
// docs/reference/interop.md.
//
// Hosted extension: <system_error> is outside the freestanding subset, so this header is
// never reached from jmpxx/core.hpp.
//
// Category identity caveat: a std::error_category is identified by the address of its
// singleton. The jmpxx categories below are held in one process-wide table, so within a
// single binary two conversions of the same error compare equal. Across a shared-library
// boundary built with hidden visibility a category can be duplicated, the same hazard
// that affects std::error_category and typeid across modules; compare on (value, domain)
// recovered through from_error_code rather than on category identity across such a
// boundary.
#ifndef JMPXX_INTEROP_ERROR_CODE_HPP
#define JMPXX_INTEROP_ERROR_CODE_HPP


#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>

namespace jmpxx {

namespace detail {

// One std::error_category per jmpxx domain. The domain is part of the category
// identity rather than folded into the value, so the (code, domain) pair survives
// the round-trip and the value keeps the full int range. The category renders a
// generic message because a bare jmpxx code carries no per-value text the library
// can name; a program that wants richer text uses the rich or type-erased policy.
class jmpxx_category final : public std::error_category {
 public:
  explicit jmpxx_category(int domain) noexcept : domain_(domain) {}
  [[nodiscard]] int domain() const noexcept { return domain_; }

  [[nodiscard]] const char* name() const noexcept override { return "jmpxx"; }
  [[nodiscard]] std::string message(int value) const override {
    return "jmpxx error " + std::to_string(value) + " (domain " +
           std::to_string(domain_) + ")";
  }

 private:
  int domain_;
};

// The process-wide table of jmpxx categories. by_domain hands out a stable
// category per domain so repeated conversions share identity, and by_address lets
// from_error_code recognize a category as jmpxx-owned and recover its domain. Both
// are guarded by one mutex, and the categories live for the program's lifetime so
// every error_code that names one stays valid.
struct category_table {
  std::mutex m;
  std::map<int, std::unique_ptr<jmpxx_category>> by_domain;
  std::map<const std::error_category*, int> by_address;

  const jmpxx_category& get(int domain) {
    std::lock_guard<std::mutex> lock(m);
    auto it = by_domain.find(domain);
    if (it == by_domain.end()) {
      auto inserted =
          by_domain.emplace(domain, std::make_unique<jmpxx_category>(domain));
      it = inserted.first;
      by_address.emplace(it->second.get(), domain);
    }
    return *it->second;
  }

  // The domain of a category if it is jmpxx-owned, or -1 if it is foreign. -1 is
  // not a valid table key, so it is an unambiguous "not ours".
  int domain_of(const std::error_category& cat) {
    std::lock_guard<std::mutex> lock(m);
    auto it = by_address.find(&cat);
    return it == by_address.end() ? -1 : it->second;
  }
};

inline category_table& categories() noexcept {
  static category_table table;
  return table;
}

}  // namespace detail

// The jmpxx error category for a domain, defaulting to the generic domain 0. Its
// identity is stable for the program's lifetime.
[[nodiscard]] inline const std::error_category& error_category(
    int domain = 0) noexcept {
  return detail::categories().get(domain);
}

// jmpxx::error -> std::error_code, in the jmpxx category for the error's domain.
// make_error_code is the same conversion under the name argument-dependent lookup
// finds, so a jmpxx::error flows into an error_code-typed slot without naming this
// function.
[[nodiscard]] inline std::error_code to_error_code(error e) noexcept {
  return std::error_code(e.code, error_category(e.domain));
}
[[nodiscard]] inline std::error_code make_error_code(error e) noexcept {
  return to_error_code(e);
}

// std::error_code -> jmpxx::error. What each direction preserves, and what a foreign
// category loses, are in docs/reference/interop.md. is_jmpxx is exposed beside it
// because a caller that must not narrow a foreign code needs to ask before it
// converts rather than after.
[[nodiscard]] inline bool is_jmpxx(const std::error_code& ec) noexcept {
  return detail::categories().domain_of(ec.category()) >= 0;
}
[[nodiscard]] inline error from_error_code(const std::error_code& ec) noexcept {
  int domain = detail::categories().domain_of(ec.category());
  return domain >= 0 ? error(ec.value(), domain) : error(ec.value(), 0);
}

}  // namespace jmpxx

#endif  // JMPXX_INTEROP_ERROR_CODE_HPP
// from jmpxx/interop/exception.hpp
// SPDX-License-Identifier: MIT
// Opt-in boundary between a propagated jmpxx failure and a thrown C++ exception.
//
// jmpxx is for code compiled with exceptions disabled, so this bridge is the boundary
// where exception-free code must call into, or be called from, a component that
// signals failure by throwing. value_or_throw turns a failure into a throw at the
// point a region crosses into exception-based code, and catch_into_result runs a
// callable that may throw and converts the outcome back into a result, so the rest of
// the program keeps propagating failures through jmpxx.
//
// The error travels inside error_exception<E>, a carrier that holds E by value and
// derives from std::exception, so a generic handler upstream still catches it while
// the round trip recovers the same E. docs/reference/interop.md states what each
// direction promises.
//
// The whole bridge exists only where exceptions are enabled, which is the opposite
// of the primary no-exceptions use case, so it is fenced behind JMPXX_HAS_EXCEPTIONS
// and is absent from a -fno-exceptions or freestanding build. Including it in such a
// build defines JMPXX_INTEROP_HAS_EXCEPTION_BRIDGE to 0 and declares nothing, so a
// translation unit can include it unconditionally and query the macro.
#ifndef JMPXX_INTEROP_EXCEPTION_HPP
#define JMPXX_INTEROP_EXCEPTION_HPP


#if JMPXX_HAS_EXCEPTIONS
#define JMPXX_INTEROP_HAS_EXCEPTION_BRIDGE 1

#include <exception>
#include <type_traits>

namespace jmpxx {

// Carries a jmpxx error E across a throw boundary and back without loss. Deriving
// from std::exception lets an upstream generic handler catch it, while a handler
// that knows jmpxx recovers the error through error().
template <class E>
class error_exception : public std::exception {
 public:
  explicit error_exception(const E& e) : err_(e) {}
  explicit error_exception(E&& e) noexcept(
      std::is_nothrow_move_constructible_v<E>)
      : err_(static_cast<E&&>(e)) {}

  [[nodiscard]] const E& error() const& noexcept { return err_; }
  [[nodiscard]] E& error() & noexcept { return err_; }

  [[nodiscard]] const char* what() const noexcept override {
    return "jmpxx::error_exception";
  }

 private:
  E err_;
};

// result -> value, throwing on failure. Used where exception-free code must hand a
// failure to a component that expects a throw: on success the value is returned,
// and on failure error_exception<E> carrying the error is thrown.
template <class T, class E>
T value_or_throw(result<T, E> r) {
  if (r) {
    if constexpr (std::is_void_v<T>)
      return;
    else
      return detail::move(r).assume_value();
  }
  throw error_exception<E>(detail::move(r).assume_error());
}

// callable -> result, catching a throw at the boundary. f is run; on a normal
// return its value (or void) becomes a success result. A thrown error_exception<E>
// is recovered losslessly as its error. Any other exception is mapped to an E by
// make_error, the caller's policy for "a foreign component threw", so the failure
// path stays in jmpxx terms. The function never propagates an exception itself.
template <class E, class F, class MakeError>
auto catch_into_result(F&& f, MakeError&& make_error) noexcept
    -> result<decltype(static_cast<F&&>(f)()), E> {
  using T = decltype(static_cast<F&&>(f)());
  try {
    if constexpr (std::is_void_v<T>) {
      static_cast<F&&>(f)();
      return result<void, E>{};
    } else {
      return result<T, E>(in_place, static_cast<F&&>(f)());
    }
  } catch (const error_exception<E>& je) {
    return result<T, E>(fail(je.error()));
  } catch (...) {
    return result<T, E>(fail(static_cast<MakeError&&>(make_error)()));
  }
}

}  // namespace jmpxx

#else
#define JMPXX_INTEROP_HAS_EXCEPTION_BRIDGE 0
#endif  // JMPXX_HAS_EXCEPTIONS

#endif  // JMPXX_INTEROP_EXCEPTION_HPP
// from jmpxx/interop/expected.hpp
// SPDX-License-Identifier: MIT
// Bridge between result<T, E> and std::expected<T, E>.
//
// A function returning std::expected is converted to a result at the call site, and
// a result is handed back out as a std::expected at the boundary. The conversion is
// lossless in both directions, because both types carry exactly a value or the same
// error type E, and the round-trip preserves which alternative is held and its
// contents.
//
// This bridge lives in its own header and is never pulled in by jmpxx/core.hpp, so
// the minimal core's include graph stays free of <expected>; that separation is
// what keeps the freestanding promise.
//
// std::expected became freestanding only in C++26, but <expected> itself compiles
// in a freestanding configuration on the supported toolchains, and the conversions
// below touch only its freestanding-clean surface: they construct from a value or a
// std::unexpected, and read through operator* and error(). They never call
// std::expected::value(), which is the one member that can throw and which a strict
// freestanding implementation deletes, so the bridge stays usable with exceptions
// disabled and in a freestanding build.
//
// The bridge is available wherever std::expected is, detected by __cpp_lib_expected.
// It deliberately does not require __cpp_lib_freestanding_expected, which a current
// libstdc++ defines only from GCC 14 and libc++ does not define at all, even though
// <expected> is present and freestanding-usable on both.
#ifndef JMPXX_INTEROP_EXPECTED_HPP
#define JMPXX_INTEROP_EXPECTED_HPP


#if defined(__has_include)
#if __has_include(<version>)
#include <version>
#endif
#endif

#if defined(__cpp_lib_expected)
#define JMPXX_INTEROP_HAS_EXPECTED 1

#include <expected>
#include <type_traits>

namespace jmpxx {

// result<T, E> -> std::expected<T, E>. A held value becomes the expected value and
// a held failure becomes std::unexpected, with the error moved or copied through. The
// state is read with the narrow accessors after the discriminant is checked, so no
// checked-access termination path is taken.
template <class T, class E>
[[nodiscard]] constexpr std::expected<T, E> to_expected(const result<T, E>& r) {
  if (r.has_value()) {
    if constexpr (std::is_void_v<T>)
      return std::expected<T, E>{};
    else
      return std::expected<T, E>(std::in_place, r.assume_value());
  }
  return std::expected<T, E>(std::unexpect, r.assume_error());
}

template <class T, class E>
[[nodiscard]] constexpr std::expected<T, E> to_expected(result<T, E>&& r) {
  if (r.has_value()) {
    if constexpr (std::is_void_v<T>)
      return std::expected<T, E>{};
    else
      return std::expected<T, E>(std::in_place,
                                 detail::move(r).assume_value());
  }
  return std::expected<T, E>(std::unexpect, detail::move(r).assume_error());
}

// std::expected<T, E> -> result<T, E>. The value is read through operator* and the
// error through error(); neither throws, so the conversion is clean with exceptions
// disabled. std::expected::value() is never called.
template <class T, class E>
[[nodiscard]] constexpr result<T, E> from_expected(
    const std::expected<T, E>& e) {
  if (e.has_value()) {
    if constexpr (std::is_void_v<T>)
      return result<T, E>{};
    else
      return result<T, E>(in_place, *e);
  }
  return result<T, E>(fail(e.error()));
}

template <class T, class E>
[[nodiscard]] constexpr result<T, E> from_expected(std::expected<T, E>&& e) {
  if (e.has_value()) {
    if constexpr (std::is_void_v<T>)
      return result<T, E>{};
    else
      return result<T, E>(in_place, *static_cast<std::expected<T, E>&&>(e));
  }
  return result<T, E>(fail(static_cast<std::expected<T, E>&&>(e).error()));
}

}  // namespace jmpxx

#else
#define JMPXX_INTEROP_HAS_EXPECTED 0
#endif  // __cpp_lib_expected

#endif  // JMPXX_INTEROP_EXPECTED_HPP

#endif  // JMPXX_INTEROP_HPP

#endif  // JMPXX_AMALGAMATED_HPP

