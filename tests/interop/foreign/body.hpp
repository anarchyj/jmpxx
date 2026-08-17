// SPDX-License-Identifier: MIT
// What each foreign-header fixture exercises after its ecosystem's headers are in
// scope. The fixtures differ only in whose headers come first; the surface they put
// under those headers is the same one, so a collision shows up as the ecosystem that
// found it rather than as a difference in what was tried.
//
// The surface here is chosen for what a macro can reach: a type alias used in a
// call-shaped position, a member named like a common macro, an enumerator, and every
// public entry point a consumer writes at a boundary. Compiling is the test.
#ifndef JMPXX_TEST_FOREIGN_BODY_HPP
#define JMPXX_TEST_FOREIGN_BODY_HPP

#include <jmpxx/core.hpp>
#include <jmpxx/diagnostics.hpp>
#include <jmpxx/erased.hpp>
#include <jmpxx/interop/adapt.hpp>

namespace foreign_probe {

enum class step { ok = 0, absent = 1, out_of_range = 2 };

inline jmpxx::result<int> produce(int x) {
  if (x < 0) return jmpxx::fail(jmpxx::error(static_cast<int>(step::absent), 7));
  return x * 2;
}

inline jmpxx::result<int> forward(int x) {
  JMPXX_TRY(v, produce(x));
  return v + 1;
}

inline jmpxx::result<int> forward_void(int x) {
  JMPXX_TRYV(produce(x));
  return x;
}

// The monadic chain is where a firmware consumer's U(v) macro reached a type alias in
// a call-shaped position and made the header uncompilable behind their own.
inline jmpxx::result<int> chain(int x) {
  return produce(x)
      .transform([](int v) { return v + 1; })
      .and_then([](int v) { return jmpxx::result<int>(v * 2); })
      .or_else([](jmpxx::error e) { return jmpxx::result<int>(jmpxx::fail(e)); });
}

inline jmpxx::result<int, jmpxx::erased_error> at_boundary(int x) {
  if (x < 0)
    return jmpxx::fail(jmpxx::erased_error(static_cast<int>(step::out_of_range), 9));
  return x;
}

inline jmpxx::result<int, jmpxx::rich_error> with_context(int x) {
  if (x < 0) return jmpxx::fail(jmpxx::rich_error(1, 2));
  return x;
}

inline int drive() {
  jmpxx::landing root;
  int total = 0;
  total += forward(3).value_or(0);
  total += forward_void(4).value_or(0);
  total += chain(5).value_or(0);
  total += at_boundary(-1).has_value() ? 0 : 1;
  total += with_context(-1).has_value() ? 0 : 1;
  int present = 7;
  total += jmpxx::from_optional<jmpxx::error>(&present,
                                              [] { return jmpxx::error(3); })
               .value_or(0);
  total += jmpxx::from_condition<jmpxx::error>(
               true, [] { return 1; }, [] { return jmpxx::error(4); })
               .value_or(0);
  return total;
}

}  // namespace foreign_probe

#endif  // JMPXX_TEST_FOREIGN_BODY_HPP
