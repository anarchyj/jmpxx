// SPDX-License-Identifier: MIT
// A deliberately regressed unwind arm: the inverted subject the arm's tiers are aimed at
// to prove they still catch the defect classes that have actually shipped here before.
//
// It exposes the same escape_scope and eject shape as jmpxx::unwind, so a fixture is
// written once and built twice, against the real arm and against this one. A tier that
// passes both is not a tier. Two defects are selected by macro, and each was confirmed to
// make its tier fail before it was kept:
//
//   JMPXX_KNOWN_BAD_NOTHROW_EJECT    eject is declared so a caller can prove it neither
//                                    returns nor unwinds, which lets the optimizer drop
//                                    the cleanup landing pads the forced unwind runs.
//                                    From -O1 upward the escape then reaches its landing
//                                    with no destructor run at all.
//   JMPXX_KNOWN_BAD_SHARED_CARRIER   the active-scope state is one global rather than
//                                    per-thread, so concurrent scopes land in each
//                                    other's frames.
//
// The first is stated as nothrow rather than as the bare [[noreturn]] the arm declines,
// because [[noreturn]] alone no longer reaches the elision on GCC 13 or Clang 16 through
// 19: those compilers keep the pads while the call may still unwind. Nothrow removes the
// exceptional edge outright, which is the property the arm's shape defends and the one an
// inverted subject has to remove to be honest.
//
// The arm's third defence, the noinline trampoline that keeps the body's automatic objects
// out of the landing frame, has no inverted subject here. One was written and it did not
// reproduce: forcing the body inline still destroyed its objects correctly on GCC 13 and
// Clang 18 at every optimization level, so the switch was removed rather than kept as a
// case that proves nothing. That defence is therefore asserted by the trampoline's own
// comment and by the destructor counts, and not by a failing subject. Recording the gap is
// the point; a known-bad case that cannot fail is worse than none, because it reads as
// coverage.
//
// The duplication of the arm's mechanism is deliberate. The shipped header carries no
// defect switch, so the inverted subject has to be its own implementation; it is kept to
// the minimum a defect can hide in and shares the real arm's payload rules and nothing
// else.
#ifndef JMPXX_TEST_KNOWN_BAD_REGRESSED_ARM_HPP
#define JMPXX_TEST_KNOWN_BAD_REGRESSED_ARM_HPP

#include "jmpxx/core.hpp"

#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <unwind.h>

namespace jmpxx_known_bad {
namespace detail {

struct carrier {
  carrier* prev = nullptr;
  bool ejected = false;
  alignas(16) unsigned char error[32];
  std::jmp_buf buf;
  _Unwind_Exception exception;
};

#if JMPXX_KNOWN_BAD_SHARED_CARRIER
inline carrier*& active() noexcept {
  static carrier* top = nullptr;  // the defect: shared by every thread
  return top;
}
#else
inline carrier*& active() noexcept {
  static thread_local carrier* top = nullptr;
  return top;
}
#endif

inline _Unwind_Reason_Code stop_function(int, _Unwind_Action actions,
                                         _Unwind_Exception_Class, _Unwind_Exception*,
                                         _Unwind_Context* ctx, void* parameter) noexcept {
  carrier* car = static_cast<carrier*>(parameter);
  auto cfa = static_cast<std::uintptr_t>(_Unwind_GetCFA(ctx));
  if (cfa > reinterpret_cast<std::uintptr_t>(car) || (actions & _UA_END_OF_STACK))
    std::longjmp(car->buf, 1);
  return _URC_NO_REASON;
}

inline void escape_cleanup(_Unwind_Reason_Code, _Unwind_Exception*) noexcept {
  jmpxx::platform::fail_fast("known-bad arm: escape swallowed");
}

#if JMPXX_KNOWN_BAD_NOTHROW_EJECT
// Inlineable, noreturn, and nothrow: with no exceptional edge left at the call site the
// optimizer drops the cleanup landing pads. The nothrow attribute rather than noexcept,
// so the elision is isolated from the terminate barrier an empty exception specification
// would also install inside this frame.
[[noreturn]] __attribute__((nothrow)) inline void drive_unwind(carrier* car) {
  std::memset(&car->exception, 0, sizeof(car->exception));
#ifdef __ARM_EABI_UNWINDER__
  const char cls[8] = {'j', 'm', 'p', 'x', 'x', 'B', 'a', 'd'};
  std::memcpy(car->exception.exception_class, cls, sizeof(cls));
#else
  car->exception.exception_class = 0x6a6d70787842616dULL;
#endif
  car->exception.exception_cleanup = escape_cleanup;
  _Unwind_ForcedUnwind(&car->exception, stop_function, car);
  jmpxx::platform::fail_fast("known-bad arm: the forced unwind did not reach a landing");
}
#else
JMPXX_NOINLINE inline void drive_unwind(carrier* car) {
  std::memset(&car->exception, 0, sizeof(car->exception));
#ifdef __ARM_EABI_UNWINDER__
  const char cls[8] = {'j', 'm', 'p', 'x', 'x', 'B', 'a', 'd'};
  std::memcpy(car->exception.exception_class, cls, sizeof(cls));
#else
  car->exception.exception_class = 0x6a6d70787842616dULL;
#endif
  car->exception.exception_cleanup = escape_cleanup;
  _Unwind_ForcedUnwind(&car->exception, stop_function, car);
  static volatile bool reached = true;
  if (reached)
    jmpxx::platform::fail_fast("known-bad arm: the forced unwind did not reach a landing");
}
#endif

struct never {
  template <class T>
  [[noreturn]] operator T() const {
    jmpxx::platform::fail_fast("known-bad arm: eject returned");
  }
};

template <class T, class E, class Body>
JMPXX_NOINLINE jmpxx::result<T, E> run_body(Body&& body) {
  if constexpr (std::is_void_v<T>) {
    static_cast<Body&&>(body)();
    return jmpxx::result<T, E>{};
  } else {
    return jmpxx::result<T, E>(jmpxx::in_place, static_cast<Body&&>(body)());
  }
}

}  // namespace detail

template <class E>
#if JMPXX_KNOWN_BAD_NOTHROW_EJECT
[[noreturn]] __attribute__((nothrow)) inline void
#else
detail::never
#endif
eject(E err) {
  detail::carrier* car = detail::active();
  if (!car) jmpxx::platform::fail_fast("known-bad arm: no active scope");
  std::memcpy(car->error, &err, sizeof(E));
  car->ejected = true;
  detail::drive_unwind(car);
#if !JMPXX_KNOWN_BAD_NOTHROW_EJECT
  return detail::never{};
#endif
}

template <class E = jmpxx::error, class Body>
[[nodiscard]] auto escape_scope(Body&& body)
    -> jmpxx::result<decltype(static_cast<Body&&>(body)()), E> {
  using T = decltype(static_cast<Body&&>(body)());
  detail::carrier car;
  car.prev = detail::active();
  detail::active() = &car;
  struct pop {
    detail::carrier* c;
    ~pop() { detail::active() = c->prev; }
  } restore{&car};

  if (setjmp(car.buf) == 0) return detail::run_body<T, E>(static_cast<Body&&>(body));
  E err;
  std::memcpy(&err, car.error, sizeof(E));
  return jmpxx::result<T, E>(jmpxx::fail(err));
}

}  // namespace jmpxx_known_bad

#endif  // JMPXX_TEST_KNOWN_BAD_REGRESSED_ARM_HPP
