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

#include "jmpxx/core/config.hpp"
#include "jmpxx/core/detail/utility.hpp"
#include "jmpxx/core/transport.hpp"

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
