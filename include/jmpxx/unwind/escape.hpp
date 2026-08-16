// SPDX-License-Identifier: MIT
// The experimental non-local unwind arm: the caller-facing surface.
//
// escape_scope marks the single landing boundary a region's escapes return to, and
// eject performs the non-local escape from any call depth below it. Between them the
// platform unwinder runs the destructor of every automatic object on the path, so the
// intermediate frames need no propagation construct in their source.
//
// This surface is reached only through jmpxx/unwind.hpp, which refuses a configuration
// without unwind cleanup tables before this header is seen. It is never on a default
// path: a program uses it only by including that header and naming these functions.
#ifndef JMPXX_UNWIND_ESCAPE_HPP
#define JMPXX_UNWIND_ESCAPE_HPP

#include "jmpxx/core/error.hpp"
#include "jmpxx/core/transport.hpp"
#include "jmpxx/platform/trap.hpp"
#include "jmpxx/unwind/backend.hpp"

#include <cstddef>
#include <type_traits>

namespace jmpxx {
namespace unwind {

// Whether the arm has a working backend on this target ABI. False on a target with no
// supported forced-unwind mechanism, where escape_scope and eject refuse instantiation
// with a stated precondition. A program can branch on this to fall back to the portable
// surface rather than failing to compile.
[[nodiscard]] constexpr bool available() noexcept {
  return JMPXX_UNWIND_AVAILABLE != 0;
}

namespace detail {

// The constraints the arm places on an error type. It travels through a fixed,
// trivially copyable carrier, so it must be small and trivially copyable; these are
// satisfied by every error representation the library ships and by any plain value or
// enum a caller is likely to escape with.
template <class E>
inline constexpr bool valid_error_type =
    std::is_trivially_copyable_v<E> && sizeof(E) <= payload_capacity &&
    alignof(E) <= payload_align;

// Read the ejected error back out of the carrier. E is trivially copyable, so the
// bytes eject stored are a valid E and need no default construction of E.
template <class E>
[[nodiscard]] inline E read_error(const carrier& car) noexcept {
  // E is trivially copyable (valid_error_type), so a bit-cast turns the carried bytes
  // back into an E with defined behavior and no aliasing or object-lifetime subtlety.
  // __builtin_bit_cast is used rather than std::bit_cast so the arm needs no <bit>,
  // which the older WebAssembly toolchain's libc++ does not provide; every supported
  // compiler has the builtin, the same way the core already uses __builtin_addressof.
  // The byte holder is a plain aggregate rather than std::array, so the arm needs no
  // <array> either, which keeps its include set to the freestanding headers a trusted
  // application and a bare-metal C library can both provide.
  struct bytes_of {
    unsigned char raw[sizeof(E)];
  } bytes;
  copy_bytes(bytes.raw, car.error, sizeof(E));
  return __builtin_bit_cast(E, bytes);
}

// Run the body in a frame distinct from the landing frame, returning the success
// result. This must not be inlined into escape_scope: the body's automatic objects
// must live in a frame the forced unwind destroys, not in the landing frame the
// landing resumes. If the body inlined into the landing frame, its objects would be
// constructed between the landing's setjmp and the resuming longjmp and the longjmp
// would skip their destructors, the RAII break the arm exists to prevent.
//
// The destructor-count tiers would catch an imbalance from this cause, but unlike the
// arm's other defences it has no inverted subject proving they do: one was written and
// forcing the body inline still destroyed its objects correctly on every compiler and
// optimization level tried. This attribute therefore rests on the reasoning above rather
// than on a case that fails without it, which is a weaker footing and is why it is said
// here plainly.
template <class T, class E, class Body>
JMPXX_NOINLINE result<T, E> run_body(Body&& body) {
  if constexpr (std::is_void_v<T>) {
    static_cast<Body&&>(body)();
    return result<T, E>{};
  } else {
    return result<T, E>(in_place, static_cast<Body&&>(body)());
  }
}

}  // namespace detail

// Eject a failure to the innermost escape_scope on the current thread. The platform
// unwinder runs every intermediate destructor, then control resumes at that scope,
// whose result holds this error. Never returns.
//
// Preconditions, each a defined fail-fast rather than undefined behavior: an
// escape_scope is active on this thread, and its error type is E. The type check is
// the validation of the eject payload boundary, so a mismatched escape is diagnosed
// rather than reinterpreting one error type's bytes as another's.
//
// The error type E must match the receiving escape_scope<E>. On the library-driven
// ABIs the escape transits a frame's typed catch and a cooperative catch-all (one
// that rethrows abi::__forced_unwind); a catch-all that swallows it without rethrowing
// is turned into a diagnosed termination rather than a silent wrong landing. A frame on
// the path must not be marked noexcept, which would terminate the unwind there.
template <class E>
detail::never eject(E err) {
  static_assert(available(),
                "the jmpxx unwind arm has no backend on this target ABI; guard its "
                "use with jmpxx::unwind::available() or use the portable surface");
  static_assert(detail::valid_error_type<E>,
                "the unwind arm error type must be small and trivially copyable");
  detail::carrier* car = detail::active();
  if (!car)
    platform::fail_fast(
        "jmpxx::unwind::eject: no active escape_scope on this thread");
  if (car->error_tag != &detail::type_tag<E>)
    platform::fail_fast(
        "jmpxx::unwind::eject: error type does not match the active escape_scope");
  if (detail::escape_in_flight())
    platform::fail_fast(
        "jmpxx::unwind::eject: an escape is already unwinding on this thread; a "
        "destructor running as its cleanup cannot start a second escape");
  detail::copy_bytes(car->error, &err, sizeof(E));
  car->ejected = true;
  detail::escape_in_flight() = true;
#if JMPXX_UNWIND_BACKEND_WASM
  detail::throw_escape();
#elif JMPXX_UNWIND_AVAILABLE
  detail::drive_unwind(car);
#endif
  // Not reached at run time: the backend above transfers control to the landing. The
  // return keeps the optimizer from concluding that eject can neither return nor unwind,
  // which is the proof that would delete the caller's cleanup landing pads.
  return detail::never{};
}

// Run body within a landing boundary and report the outcome as a result<T, E>. If body
// returns normally its value becomes the result's value; if any frame below it calls
// eject<E>, the stack unwinds back here running every destructor on the path and the
// result holds that error. T is deduced from body's return type and may be void.
//
// The scope registers itself as the innermost landing on the current thread for the
// duration of body and restores the previous one on exit, so scopes nest: an eject
// lands at the nearest enclosing escape_scope. The carrier lives in this frame, so the
// escape allocates nothing.
template <class E = error, class Body>
[[nodiscard]] auto escape_scope(Body&& body)
    -> result<decltype(static_cast<Body&&>(body)()), E> {
  static_assert(available(),
                "the jmpxx unwind arm has no backend on this target ABI; guard its "
                "use with jmpxx::unwind::available() or use the portable surface");
  static_assert(detail::valid_error_type<E>,
                "the unwind arm error type must be small and trivially copyable");
  using T = decltype(static_cast<Body&&>(body)());

  detail::carrier car;
  car.prev = detail::active();
  car.error_tag = &detail::type_tag<E>;
  detail::active() = &car;
  // Restore the active-scope stack on every exit, normal or ejected. This guard is in
  // the landing frame, which the unwind stops at, so it is itself not unwound.
  struct scope_pop {
    detail::carrier* c;
    ~scope_pop() { detail::active() = c->prev; }
  } restore{&car};

  // A non-local escape that reaches this scope's landing is handled below per backend.
  // An escape that an intervening catch-all swallowed without rethrowing never reaches
  // the landing, and the body returns as if it had succeeded; the carrier's ejected
  // flag records that an eject happened, so a body that returns normally while the flag
  // is set is a swallowed escape. Turning that into a defined fail-fast keeps a swallow
  // from silently returning a value the program never produced. On the library-driven
  // ABIs the forced-unwind cleanup also fail-fasts at the point of the swallow; this
  // check additionally covers WebAssembly, where the escape is an ordinary throw.
#if JMPXX_UNWIND_BACKEND_WASM
  try {
    result<T, E> produced = detail::run_body<T, E>(static_cast<Body&&>(body));
    if (car.ejected) detail::report_swallowed_escape();
    return produced;
  } catch (const detail::escape_signal&) {
    detail::escape_in_flight() = false;
    return result<T, E>(fail(detail::read_error<E>(car)));
  }
#elif JMPXX_UNWIND_AVAILABLE
  // setjmp returns 0 on the direct path and 1 when the stop function lands the unwind
  // here. On the direct path the body runs in its own frame; on the landing path the
  // ejected error is read from the carrier. The landing frame is identified by the
  // carrier's address during the unwind, so there is no separate capture step.
  if (setjmp(car.buf) == 0) {
    result<T, E> produced = detail::run_body<T, E>(static_cast<Body&&>(body));
    if (car.ejected) detail::report_swallowed_escape();
    return produced;
  }
  // The landing has been reached, so this thread is no longer unwinding and may escape
  // again.
  detail::escape_in_flight() = false;
  return result<T, E>(fail(detail::read_error<E>(car)));
#else
  // No backend on this target. The static assertion above is the diagnostic; the body
  // exists only so the template still parses, and it cannot run.
  return detail::run_body<T, E>(static_cast<Body&&>(body));
#endif
}

}  // namespace unwind
}  // namespace jmpxx

#endif  // JMPXX_UNWIND_ESCAPE_HPP
