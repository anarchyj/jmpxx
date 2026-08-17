// SPDX-License-Identifier: MIT
// The std::expected bridge under a freestanding, no-exceptions, no-RTTI build. It
// checks that including the bridge adds no non-freestanding dependency the minimal
// core did not already allow, and that the conversions stay clean with exceptions
// disabled because they never call std::expected::value(). Trivial payloads only, so
// no hosted facility is needed; success is reported through the exit code.
//
// <expected> became freestanding in C++26, but compiles in a freestanding
// configuration on the supported toolchains today, so this fixture is built at
// C++23 with -ffreestanding to exercise the bridge where the matrix can reach it.
#include "jmpxx/core.hpp"
#include "jmpxx/interop/adapt.hpp"
#include "jmpxx/interop/expected.hpp"

#include <expected>

using namespace jmpxx;

static result<int, error> leaf(int x) {
  if (x < 0) return fail(error(42));
  return x * 2;
}

int main() {
  // result -> std::expected -> result, read through operator* (never value()).
  std::expected<int, error> e = to_expected(leaf(5));
  if (!e.has_value() || *e != 10) return 1;
  std::expected<int, error> eb = to_expected(leaf(-1));
  if (eb.has_value() || eb.error().code != 42) return 2;
  result<int, error> back = from_expected(eb);
  if (back.has_value() || back.error().code != 42) return 3;

  // The optional-like adapter is freestanding too (duck-typed over a pointer).
  int v = 7;
  result<int, error> p = from_optional<error>(&v, [] { return error(1); });
  if (!p.has_value() || p.value() != 7) return 4;
  return 0;
}
