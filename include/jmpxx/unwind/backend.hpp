// SPDX-License-Identifier: MIT
// The experimental non-local unwind arm: per-ABI backend.
//
// Fenced home for the arm's ABI-specific constructs. The platform-fence scan permits
// target macros and platform headers here, under jmpxx/unwind/, exactly as it permits
// them under jmpxx/platform/. Nothing on the portable core path reaches this header; a
// consumer compiles it only by opting into the arm through jmpxx/unwind.hpp.
//
// The arm drives the platform forced-unwind facility so a failure ejected deep in a
// call chain returns to a registered landing while the unwinder runs every destructor
// on the path, at the cost of requiring unwind cleanup tables. One of three backends is
// selected by target, none on a default path: Itanium and EHABI forced unwind on GCC and
// Clang, an unwinding longjmp on MSVC, and a typed throw the virtual machine unwinds on
// WebAssembly. The per-ABI guarantees, the WebAssembly divergences, and the precondition
// are in docs/reference/unwind.md; this file carries the rationale at each construct that
// the reference does not.
#ifndef JMPXX_UNWIND_BACKEND_HPP
#define JMPXX_UNWIND_BACKEND_HPP

#include "jmpxx/core/config.hpp"
#include "jmpxx/platform/detect.hpp"
#include "jmpxx/platform/trap.hpp"

#include <cstddef>
#include <cstdint>
#if JMPXX_COMPILER_MSVC
#include <cstring>  // the byte helpers below use the library where no builtin exists
#endif

// Backend selection. Exactly one JMPXX_UNWIND_BACKEND_* is 1 when the arm is
// available; all are 0 on a target with no supported backend, where the public API
// refuses use with a stated precondition rather than silently doing nothing.
#if JMPXX_ARCH_WASM
// WebAssembly has no forced-unwind primitive, so the escape is a typed throw the virtual
// machine unwinds and the landing catches a private signal type. The escape is therefore
// an ordinary catchable exception, which diverges the catch-all and sad-path guarantees
// from the library-driven ABIs; docs/reference/unwind.md states the divergence.
#define JMPXX_UNWIND_BACKEND_WASM 1
#define JMPXX_UNWIND_BACKEND_ITANIUM 0
#define JMPXX_UNWIND_BACKEND_SEH 0
#define JMPXX_UNWIND_AVAILABLE 1
#elif JMPXX_COMPILER_MSVC
// Native MSVC drives the cleanup through the structured-exception unwinder.
#define JMPXX_UNWIND_BACKEND_WASM 0
#define JMPXX_UNWIND_BACKEND_ITANIUM 0
#define JMPXX_UNWIND_BACKEND_SEH 1
#define JMPXX_UNWIND_AVAILABLE 1
#elif (JMPXX_COMPILER_GCC || JMPXX_COMPILER_CLANG) && defined(__SEH__)
// A GCC or Clang toolchain targeting Windows with structured exception handling, which
// is what MinGW-w64 uses on x86-64. It declares _Unwind_ForcedUnwind, so the arm would
// link, but libgcc's structured-exception unwinder does not implement it: the call
// returns _URC_END_OF_STACK without ever invoking the stop function or running a single
// cleanup, verified live under Wine with GCC 13, while an ordinary throw on the same
// toolchain unwinds correctly. An arm that links and then cannot escape is worse than one
// that refuses, so the backend is absent here and available() is false.
#define JMPXX_UNWIND_BACKEND_WASM 0
#define JMPXX_UNWIND_BACKEND_ITANIUM 0
#define JMPXX_UNWIND_BACKEND_SEH 0
#define JMPXX_UNWIND_AVAILABLE 0
#elif JMPXX_COMPILER_GCC || JMPXX_COMPILER_CLANG
// GCC and Clang provide the Itanium and EHABI _Unwind_ForcedUnwind interface through
// libgcc or libunwind on Linux, macOS, and the BSDs.
#define JMPXX_UNWIND_BACKEND_WASM 0
#define JMPXX_UNWIND_BACKEND_ITANIUM 1
#define JMPXX_UNWIND_BACKEND_SEH 0
#define JMPXX_UNWIND_AVAILABLE 1
#else
#define JMPXX_UNWIND_BACKEND_WASM 0
#define JMPXX_UNWIND_BACKEND_ITANIUM 0
#define JMPXX_UNWIND_BACKEND_SEH 0
#define JMPXX_UNWIND_AVAILABLE 0
#endif

// The setjmp-based backends resume the landing with a jump buffer planted in the
// landing frame. The forced unwind runs the intermediate cleanups first; the longjmp
// then discards only the already-cleaned region back to the landing. Platform
// thread-cancellation unwinders use this control transfer; it is the supported pattern,
// not the bare longjmp the library otherwise forbids.
#if JMPXX_UNWIND_BACKEND_ITANIUM || JMPXX_UNWIND_BACKEND_SEH
#include <csetjmp>
#endif
#if JMPXX_UNWIND_BACKEND_ITANIUM
#include <unwind.h>
#endif

namespace jmpxx {
namespace unwind {
namespace detail {

// Byte copy and fill, taken from the compiler rather than from <cstring>.
//
// The arm reaches environments whose C library is smaller than the hosted one, and an
// OP-TEE trusted application is the case that forced this: its libc provides memcpy and
// memset but not the whole of <string.h>, and libstdc++'s <cstring> declares the entire
// surface, so including it there does not compile. The builtins need no header at all and
// the compiler lowers them to the same instructions. MSVC has no such builtin, so it uses
// the library, which is available in every configuration MSVC supports here.
inline void copy_bytes(void* to, const void* from, std::size_t n) noexcept {
#if JMPXX_COMPILER_MSVC
  std::memcpy(to, from, n);
#else
  __builtin_memcpy(to, from, n);
#endif
}

inline void fill_zero(void* at, std::size_t n) noexcept {
#if JMPXX_COMPILER_MSVC
  std::memset(at, 0, n);
#else
  __builtin_memset(at, 0, n);
#endif
}

// The out-of-band carrier and landing state for one in-flight escape. It lives in
// the landing frame (it is escape_scope's own automatic object), so it costs no
// allocation and its lifetime is the landing scope. The error payload travels here,
// beside the unwind rather than inside any intermediate frame, so those frames stay
// oblivious.
//
// The payload is held type-erased in a fixed, over-aligned buffer with a type tag. A
// fixed buffer keeps the carrier allocation-free; the tag is the validation of the
// eject payload boundary, so an eject whose error type does not match the scope that
// will receive it is a defined fail-fast rather than a misread of one type's bytes as
// another's. The capacity covers every error representation the library ships and is
// asserted against E at both ends.
inline constexpr std::size_t payload_capacity = 32;
inline constexpr std::size_t payload_align = 16;

struct carrier {
  carrier* prev = nullptr;          // intrusive stack of active scopes, for nesting
  const void* error_tag = nullptr;  // identity of the error type this scope receives
  bool ejected = false;             // set by eject before it drives the unwind
  alignas(payload_align) unsigned char error[payload_capacity];
#if JMPXX_UNWIND_BACKEND_ITANIUM || JMPXX_UNWIND_BACKEND_SEH
  std::jmp_buf buf;  // resume point planted by escape_scope
#endif
#if JMPXX_UNWIND_BACKEND_ITANIUM
  // The unwind object this scope's escape travels in. It belongs to the scope rather
  // than to the thread, which docs/design/unwind-arm.md explains and which cost
  // nothing: the carrier is already the landing frame's automatic object and the
  // landing outlives every unwind that targets it.
  _Unwind_Exception exception;
#endif
};

// A distinct address per error type, used as the carrier's type tag. Two ejects
// agree with a scope only when they name the same E, which the tag enforces.
template <class E>
inline const char type_tag = 0;

// The per-thread stack of active landing scopes. eject targets the innermost one. A
// function-local thread_local is initialized on first use with no static-init
// ordering dependency, matching the diagnostic store's discipline.
inline carrier*& active() noexcept {
  static thread_local carrier* top = nullptr;
  return top;
}

// Whether an escape is unwinding on this thread right now. It is set before the unwind
// begins and cleared when the landing resumes, so code that runs as a cleanup during an
// escape can be told apart from ordinary code.
//
// The arm refuses a second escape while one is in flight. The refusal is uniform across
// the ABIs rather than allowed where it happens to work: an escape started from a cleanup
// completes on the DWARF ABIs, terminates on the ARM exception-handling ABI, and is a
// throw during unwinding on WebAssembly, which the language terminates. One contract the
// caller can rely on everywhere is worth more than a capability that aborts on a quarter
// of the supported targets, so the arm refuses it with its own diagnostic on all of them.
inline bool& escape_in_flight() noexcept {
  static thread_local bool unwinding = false;
  return unwinding;
}

// The return type of eject. eject does not return at run time and is deliberately not
// marked [[noreturn]]; docs/design/unwind-arm.md gives the reason and the measurement.
// This type is what makes that affordable: a caller writes `return unwind::eject(e)` in
// a function of any return type without a "control reaches end of non-void function"
// warning, and the conversion is never evaluated. If it ever were, it fails fast rather
// than fabricating a value.
struct never {
  template <class T>
  [[noreturn]] operator T() const {
    platform::fail_fast("jmpxx::unwind::eject returned to its caller");
  }
};

// A non-local escape that an intervening catch-all swallowed without rethrowing never
// reaches its landing. The arm turns that into a defined termination rather than a
// silent wrong landing, the same outcome the platform's own cancellation unwinder produces
// for a swallowed forced unwind.
[[noreturn]] inline void report_swallowed_escape() {
  platform::fail_fast(
      "jmpxx::unwind: a non-local escape was swallowed by a catch(...) that did not "
      "rethrow; it never reached its landing");
}

}  // namespace detail
}  // namespace unwind
}  // namespace jmpxx

// Backend: Itanium and EHABI forced unwind, used on GCC and Clang through libgcc or
// libunwind.
#if JMPXX_UNWIND_BACKEND_ITANIUM

namespace jmpxx {
namespace unwind {
namespace detail {

// An 8-byte identifier for the arm's forced-unwind exception object, the ASCII
// "jmpxxEsc". The platform personality treats any class it does not recognize as
// foreign, which is the property that lets a user's typed catch transit the unwind;
// the value is also a readable marker in a debugger. The ARM exception-handling ABI
// types the class as an 8-character array rather than a 64-bit word, so the identifier
// is spelled per ABI and written into the exception object the matching way.
#ifdef __ARM_EABI_UNWINDER__
inline constexpr char escape_class[8] = {'j', 'm', 'p', 'x', 'x', 'E', 's', 'c'};
#else
inline constexpr _Unwind_Exception_Class escape_class = 0x6a6d707878457363ULL;
#endif

// The forced-unwind stop function, and the landing marker it compares against.
//
// The landing frame is identified by the carrier's own address. The carrier is an
// automatic object in the landing frame, so its address lies inside that frame: below
// the frame's canonical frame address, and at or above the CFA of every frame the
// landing calls, including the body trampoline. The runtime calls this function for
// each frame, innermost first, before running that frame's cleanup. While the unwound
// frame's CFA is at or below the carrier it returns _URC_NO_REASON, so the runtime runs
// that frame's destructors and proceeds outward; when the CFA rises above the carrier,
// the landing frame has been reached with every intermediate destructor already run,
// and it longjmps back into the landing.
//
// Identifying the landing by address rather than by a captured frame depth is what
// makes one implementation correct across the ABIs; docs/design/unwind-arm.md gives
// the reasoning and the measurements. A destructor-count tier across optimization
// levels guards the choice.
inline _Unwind_Reason_Code stop_function(int, _Unwind_Action actions,
                                         _Unwind_Exception_Class,
                                         _Unwind_Exception*, _Unwind_Context* ctx,
                                         void* parameter) noexcept {
  carrier* car = static_cast<carrier*>(parameter);
  std::uintptr_t cfa = static_cast<std::uintptr_t>(_Unwind_GetCFA(ctx));
  std::uintptr_t marker = reinterpret_cast<std::uintptr_t>(car);
  if (cfa > marker || (actions & _UA_END_OF_STACK))
    std::longjmp(car->buf, 1);  // never returns; resumes in the landing frame
  return _URC_NO_REASON;
}

// The exception's cleanup callback fires only if the forced unwind is ever fully
// consumed rather than landed: a catch-all on the path caught it and did not rethrow.
// That swallowed escape would otherwise silently fail to reach its landing, so the arm
// turns it into a defined, diagnosed termination, exactly as the platform's own
// cancellation unwinder does. See jmpxx/unwind.hpp for the catch-all contract and the
// cooperative rethrow idiom that transits the arm.
inline void escape_cleanup(_Unwind_Reason_Code, _Unwind_Exception*) noexcept {
  report_swallowed_escape();
}

// Begin the forced unwind toward the active scope's landing, in the unwind object the
// scope owns. It does not return at run time: it either longjmps into the landing or,
// if the platform cannot unwind at all, fails fast.
//
// Two constraints bind this function, both guarded by destructor-count tiers and both
// explained in docs/design/unwind-arm.md, which also carries what was measured and when:
//   * It must not be noexcept. It is the innermost frame the forced unwind processes,
//     and an empty exception specification there terminates the unwind at that frame.
//   * It must not be provably nothrow, which is why it is noinline and why the volatile
//     guard below keeps a path on which it returns. Between them a caller cannot prove
//     the call neither returns nor unwinds, and that proof is what would delete the
//     landing pads this depends on.
JMPXX_NOINLINE inline void drive_unwind(carrier* car) {
  // Zeroing the whole object initializes the words the unwinder reserves for itself
  // (the generic ABI's private_1/private_2, the EHABI unwinder cache) without naming
  // them, then the class and cleanup are set the way each ABI types them.
  fill_zero(&car->exception, sizeof(car->exception));
#ifdef __ARM_EABI_UNWINDER__
  copy_bytes(car->exception.exception_class, escape_class, sizeof(escape_class));
#else
  car->exception.exception_class = escape_class;
#endif
  car->exception.exception_cleanup = escape_cleanup;
  _Unwind_ForcedUnwind(&car->exception, stop_function, car);
  // Reached only if the platform could not unwind at all (no cleanup tables): a
  // defined fail-fast. The volatile read cannot be folded, so the optimizer keeps a
  // path on which this function returns and does not infer it noreturn.
  static volatile bool reached_on_unwind_failure = true;
  if (reached_on_unwind_failure)
    platform::fail_fast(
        "jmpxx::unwind: the platform forced unwind did not reach a landing; the "
        "configuration is missing unwind cleanup tables");
}

}  // namespace detail
}  // namespace unwind
}  // namespace jmpxx

#endif  // JMPXX_UNWIND_BACKEND_ITANIUM

// Backend: WebAssembly typed escape signal, where the virtual machine performs the
// unwinding.
#if JMPXX_UNWIND_BACKEND_WASM

namespace jmpxx {
namespace unwind {
namespace detail {

// On WebAssembly the escape is a throw of this private type. The virtual machine
// unwinds to the landing's catch, running the destructors the compiler emits as
// catch-all-and-rethrow clauses on the way. The type is private and derives from
// nothing, so a user's typed catch never matches it; only a catch-all intercepts it,
// which is the documented WASM divergence.
struct escape_signal {};

[[noreturn]] inline void throw_escape() { throw escape_signal{}; }

}  // namespace detail
}  // namespace unwind
}  // namespace jmpxx

#endif  // JMPXX_UNWIND_BACKEND_WASM

// Backend: native MSVC structured exception handling.
#if JMPXX_UNWIND_BACKEND_SEH

namespace jmpxx {
namespace unwind {
namespace detail {

// On native MSVC the landing plants a jump buffer and the eject resumes it. MSVC's
// setjmp and longjmp participate in the structured-exception unwinder, so the longjmp
// runs the C++ destructors of the frames between the eject and the landing. This SEH
// analogue of the Itanium forced unwind is checked by the unwind-execution tier on MSVC.
[[noreturn]] inline void drive_unwind(carrier* car) {
  std::longjmp(car->buf, 1);
}

}  // namespace detail
}  // namespace unwind
}  // namespace jmpxx

#endif  // JMPXX_UNWIND_BACKEND_SEH

#endif  // JMPXX_UNWIND_BACKEND_HPP
