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

#include "jmpxx/core/config.hpp"
#include "jmpxx/core/detail/utility.hpp"
#include "jmpxx/core/transport.hpp"

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
