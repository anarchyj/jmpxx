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

#include "jmpxx/core/config.hpp"
#include "jmpxx/core/detail/utility.hpp"
#include "jmpxx/core/transport.hpp"

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
