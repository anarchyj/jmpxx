// SPDX-License-Identifier: MIT
// The public headers survive a consumer whose own headers came first.
//
// A library header is included into someone else's translation unit, after whatever they
// already included, so an identifier it uses in a call-shaped position is exposed to every
// function-like macro they have defined. The Trusted Firmware and OP-TEE projects define
// U, UL and ULL in their stdint.h as integer-suffix macros, and that convention reaches a
// large part of this library's audience. Before this fixture existed, the transport's
// monadic operations used U as a type alias and a firmware consumer could not include
// jmpxx after their own headers at all.
//
// The macros below are the real ones, spelled as those projects spell them. Compiling this
// file is the test: a public header that reaches for one of these names in a way the
// preprocessor expands stops building here rather than in a consumer's tree.
//
// This is a hosted and cross-architecture tier. It includes the diagnostic layer, which
// needs <source_location>, and the WebAssembly toolchain in the matrix does not provide
// that header at all, so the WebAssembly cell covers the same property over the core
// surface instead.
#define U(v) v##U
#define UL(v) v##UL
#define ULL(v) v##ULL

// A few more single-letter and short lowercase macros that firmware and kernel headers
// commonly define, so the surface is checked against the habit rather than one instance.
#define BIT(n) (1U << (n))
#define SHIFT_U32(v, s) ((uint32_t)(v) << (s))

#include "jmpxx/core.hpp"
#include "jmpxx/diagnostics.hpp"
#include "jmpxx/erased.hpp"
#include "jmpxx/interop/adapt.hpp"
#include "jmpxx/interop/error_code.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>

using namespace jmpxx;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("  FAIL: %s\n", what);
    ++failures;
  }
}

// The monadic chain is where the collision was, so it is exercised in full rather than
// merely instantiated: every operation, and a value that came through one of the macros.
void chain_under_macros() {
  result<int, error> r(U(1));
  auto q = r.and_then([](int v) { return result<int, error>(v + U(1)); })
               .transform([](int v) { return v * 2; })
               .or_else([](error) { return result<int, error>(0); })
               .transform_error([](error e) { return error(e.code + 1); });
  expect(q.value_or(0) == 4, "the monadic chain composes under the firmware macros");

  result<void, error> v;
  auto w = v.and_then([] { return result<int, error>(U(5)); });
  expect(w.value_or(0) == 5, "the void form composes under the firmware macros");
}

// The other public surfaces a firmware consumer is likely to reach for.
void surfaces_under_macros() {
  const auto e = error(BIT(3), 1);
  expect(e.code == 8, "the minimal error is usable under the macros");

  // The generic constructor folds the code and the domain tag into one value, so the
  // check is that it round-trips through the macro, not what the folded value is.
  const erased_error boxed{BIT(3), 0};
  expect(boxed.value() == 8, "the type-erased policy is usable under the macros");

  const std::optional<int> absent;
  const auto adapted = from_optional<error>(absent, [] { return error(U(2)); });
  expect(!adapted.has_value(), "the adapters are usable under the macros");

  const auto present = from_condition<error>(
      true, [] { return SHIFT_U32(1, 2); }, [] { return error(3); });
  expect(present.value_or(0) == 4, "the condition adapter is usable under the macros");
}

}  // namespace

int main() {
  chain_under_macros();
  surfaces_under_macros();
  std::printf("firmware_macros: %s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
