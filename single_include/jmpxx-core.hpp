// SPDX-License-Identifier: MIT
// jmpxx core, single-header amalgamation (freestanding minimal core)
//
// This file is generated from the modular headers under include/jmpxx by
// packaging/amalgamate.py. Do not edit it by hand; edit the modular headers
// and regenerate. Continuous integration regenerates and diffs it, so a stale
// single header fails the build.
#ifndef JMPXX_CORE_AMALGAMATED_HPP
#define JMPXX_CORE_AMALGAMATED_HPP

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
#define JMPXX_VERSION_PATCH 5

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

#endif  // JMPXX_CORE_AMALGAMATED_HPP

