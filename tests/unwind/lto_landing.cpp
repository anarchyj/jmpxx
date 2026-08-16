// SPDX-License-Identifier: MIT
// The landing side of the link-time-optimization tier. It counts constructions against
// destructions over escapes whose frames live in another translation unit, so an
// optimizer that elides a cleanup landing pad or moves the body into the landing frame at
// link time shows up as an imbalance rather than as a passing test.
#include "lto_chain.hpp"

#include <cstdio>

int g_ctor = 0;
int g_dtor = 0;
volatile int g_sink = 0;
int g_trip = 1;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("  FAIL: %s\n", what);
    ++failures;
  }
}

void report(const char* what, int constructed, int destructed, int expected) {
  std::printf("  %-28s constructed=%d destructed=%d expected=%d\n", what, constructed,
              destructed, expected);
}

// Eight frames below the landing, each owning a guard, all defined across the link.
void cross_unit_escape() {
  g_ctor = g_dtor = 0;
  g_trip = 1;
  auto r = arm::escape_scope<jmpxx::error>([] { return chain_top(0); });
  report("cross-unit escape", g_ctor, g_dtor, 8);
  expect(!r.has_value(), "the cross-unit escape lands as a failure");
  expect(r.error_or(jmpxx::error(0)).code == 907, "it carries the ejected error");
  expect(g_ctor == 8, "eight frames ran");
  expect(g_dtor == 8, "every guard destructed exactly once");
}

// The same chain with no eject: the value is delivered and the guards still balance.
void cross_unit_success() {
  g_ctor = g_dtor = 0;
  g_trip = 1000000;
  auto r = arm::escape_scope<jmpxx::error>([] { return chain_top(0); });
  report("cross-unit success", g_ctor, g_dtor, 8);
  expect(r.has_value(), "the no-eject chain returns its value");
  expect(g_ctor == g_dtor && g_ctor == 8, "guards balance on the success path");
}

// Self-recursion, the shape whose inlined copies lost exactly the inlined depth of
// destructors when the eject was modeled as a call that cannot unwind.
void recursive_escape() {
  g_ctor = g_dtor = 0;
  auto r = arm::escape_scope<jmpxx::error>([] { return recursive_chain(9); });
  report("recursive escape", g_ctor, g_dtor, 10);
  expect(!r.has_value() && r.error_or(jmpxx::error(0)).code == 500,
         "the recursive escape lands as its failure");
  expect(g_ctor == 10 && g_dtor == 10, "ten recursion frames destructed once each");
}

// Nested landings across the link: the inner scope catches its own escape and the outer
// one is untouched.
void nested_escape() {
  g_ctor = g_dtor = 0;
  g_trip = 1;
  auto outer = arm::escape_scope<jmpxx::error>([]() -> int {
    auto inner = arm::escape_scope<jmpxx::error>([] { return chain_top(0); });
    return inner.has_value() ? -1 : 42;
  });
  report("nested escape", g_ctor, g_dtor, 8);
  expect(outer.has_value() && outer.value_or(-1) == 42,
         "the outer scope is untouched by the inner escape");
  expect(g_ctor == g_dtor && g_ctor == 8, "guards balance across nested landings");
}

}  // namespace

int main() {
  cross_unit_escape();
  cross_unit_success();
  recursive_escape();
  nested_escape();
  std::printf("unwind_lto: %s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
