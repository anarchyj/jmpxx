// SPDX-License-Identifier: MIT
// Unwind-execution tier for the experimental non-local unwind arm. Each check drives an
// escape and observes destructor counts, the landing outcome, and handler transit. The
// fixtures use conditional ejects and distinct frames so the chain is real at the
// optimization level the build uses. CMake builds this tier at -O2, where a forced
// unwind that elides cleanup landing pads or lands at the wrong frame is caught. A
// skipped or double-run destructor here breaks RAII on the escape path, so the tier
// fails the build.
#include "jmpxx/unwind.hpp"
#include "jmpxx/core.hpp"

#include <cstdio>
#include <cstdlib>

#if JMPXX_UNWIND_BACKEND_ITANIUM && defined(__GLIBCXX__)
#include <cxxabi.h>  // abi::__forced_unwind is a libstdc++ type; libc++ has no equivalent
#endif

using namespace jmpxx;

// The arm's surface refuses instantiation where no backend exists, so a fixture that
// drives it cannot be compiled there at all. The cases below are therefore gated on the
// backend rather than on a runtime check, and a target without one reports that and
// passes: there is nothing to execute, which is itself the contract.
#if JMPXX_UNWIND_AVAILABLE

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("  FAIL: %s\n", what);
    ++g_fail;
  }
}

// An instrumented guard: balanced construction and destruction show whether a destructor
// was skipped or double-run on the escape path.
int g_ctor = 0, g_dtor = 0;
struct Guard {
  int id;
  explicit Guard(int i) : id(i) { ++g_ctor; }
  ~Guard() { ++g_dtor; }
};

// A deep chain of distinct frames doing real work, ejecting only when a runtime trip
// value is reached so the compiler cannot prove the chain noreturn and fold it. Each
// frame holds a Guard whose destructor must run exactly once on the escape.
volatile int g_sink = 0;
int g_trip = 1;
int deep_leaf(int acc) {
  Guard g{0};
  g_sink = g_sink + acc;
  if (acc >= g_trip) unwind::eject(error(900 + acc, 1));
  return acc + g.id;
}
int d1(int a) { Guard g{1}; g_sink = g_sink + a; int r = deep_leaf(a + 1); return r + g.id; }
int d2(int a) { Guard g{2}; g_sink = g_sink + a; int r = d1(a + 1); return r + g.id; }
int d3(int a) { Guard g{3}; g_sink = g_sink + a; int r = d2(a + 1); return r + g.id; }
int d4(int a) { Guard g{4}; g_sink = g_sink + a; int r = d3(a + 1); return r + g.id; }
int d5(int a) { Guard g{5}; g_sink = g_sink + a; int r = d4(a + 1); return r + g.id; }
int d6(int a) { Guard g{6}; g_sink = g_sink + a; int r = d5(a + 1); return r + g.id; }
int d7(int a) { Guard g{7}; g_sink = g_sink + a; int r = d6(a + 1); return r + g.id; }

// An escape from at least eight frames runs each destructor exactly once.
void test_deep_destructor_counts() {
  g_ctor = g_dtor = 0;
  g_trip = 1;
  auto r = unwind::escape_scope<error>([] { return d7(0); });
  check(!r.has_value(), "deep escape lands as a failure");
  check(r.error_or(error(0)).code == 907, "deep escape carries the ejected error");
  check(g_ctor == 8, "eight frames constructed their guard");
  check(g_dtor == 8, "every guard destructed exactly once (no skip, no double-run)");
  check(g_ctor == g_dtor, "construction and destruction balance on the escape path");

  // The same source, no eject: the success value is delivered and nothing leaks.
  g_ctor = g_dtor = 0;
  g_trip = 1000000;  // never reached
  auto ok = unwind::escape_scope<error>([] { return d7(0); });
  check(ok.has_value(), "no-eject chain returns its value");
  check(g_ctor == g_dtor, "construction and destruction balance on the success path");
}

// A void body that ejects.
void test_void_body() {
  g_ctor = g_dtor = 0;
  g_trip = 1;
  auto r = unwind::escape_scope<error>([]() -> void {
    Guard g{0};
    g_sink = g_sink + 1;
    unwind::eject(error(5, 2));
  });
  check(!r.has_value() && r.error_or(error(0)).code == 5, "void body ejects an error");
  check(g_ctor == 1 && g_dtor == 1, "void body guard destructed once");
}

// Nesting: an eject lands at the innermost scope; the outer scope is untouched and
// returns normally.
void test_nesting() {
  g_ctor = g_dtor = 0;
  g_trip = 1;
  auto outer = unwind::escape_scope<error>([]() -> int {
    auto inner = unwind::escape_scope<error>([] { return d3(0); });
    check(!inner.has_value(), "inner scope catches its own eject");
    return inner.has_value() ? -1 : 77;  // outer returns normally
  });
  check(outer.has_value() && outer.value_or(-1) == 77,
        "outer scope is untouched by the inner eject");
  check(g_ctor == 4 && g_dtor == 4, "nested escape ran four guards once each");
}

// Transit: a handler on the escape path does not consume the escape. A typed catch never
// matches the escape, so it transits on every backend. A cooperative catch-all that
// rethrows transits where the runtime re-raises the in-flight escape, which is libstdc++
// and the virtual-machine backend; libc++abi does not re-raise a foreign exception from a
// catch-all, so that check runs only where the transit holds. Where libstdc++ provides
// abi::__forced_unwind, the precise idiom distinguishes the forced unwind from a real
// exception. The reference records the per-ABI catch-all guarantees.
#if defined(__GLIBCXX__) || JMPXX_UNWIND_BACKEND_WASM
#define JMPXX_TEST_COOP_CATCHALL_TRANSITS 1
#else
#define JMPXX_TEST_COOP_CATCHALL_TRANSITS 0
#endif

bool g_typed_entered = false;
int transit_leaf() {
  Guard g{0};
  g_sink = g_sink + 1;
  unwind::eject(error(11));
  return 0;
}
int typed_catch_frame() {
  Guard g{1};
  try {
    return transit_leaf();
  } catch (int) {
    g_typed_entered = true;  // must not happen: the escape is not an int
    return -1;
  }
}

#if JMPXX_TEST_COOP_CATCHALL_TRANSITS
int coop_catchall_frame() {
  Guard g{1};
  try {
    return transit_leaf();
  } catch (...) {
    throw;  // cooperative: re-raise the in-flight escape so it transits
  }
}
#endif

#if JMPXX_UNWIND_BACKEND_ITANIUM && defined(__GLIBCXX__)
bool g_idiom_real_entered = false;
int forced_unwind_idiom_frame() {
  Guard g{1};
  try {
    return transit_leaf();
  } catch (const abi::__forced_unwind&) {
    throw;  // re-raise only the forced unwind
  } catch (...) {
    g_idiom_real_entered = true;  // must not happen: no real exception occurred
    return -1;
  }
}
#endif

void test_catch_all_transit() {
  g_ctor = g_dtor = 0;
  g_typed_entered = false;
  auto r1 = unwind::escape_scope<error>([] { return typed_catch_frame(); });
  check(!r1.has_value() && r1.error_or(error(0)).code == 11,
        "escape transits a typed catch and reaches the landing");
  check(!g_typed_entered, "typed catch did not consume the escape");
  check(g_ctor == g_dtor, "guards balanced across the typed-catch transit");

#if JMPXX_TEST_COOP_CATCHALL_TRANSITS
  g_ctor = g_dtor = 0;
  auto r2 = unwind::escape_scope<error>([] { return coop_catchall_frame(); });
  check(!r2.has_value() && r2.error_or(error(0)).code == 11,
        "escape transits a cooperative catch-all and reaches the landing");
  check(g_ctor == g_dtor, "guards balanced across the cooperative catch-all transit");
#endif

#if JMPXX_UNWIND_BACKEND_ITANIUM && defined(__GLIBCXX__)
  g_ctor = g_dtor = 0;
  g_idiom_real_entered = false;
  auto r3 = unwind::escape_scope<error>([] { return forced_unwind_idiom_frame(); });
  check(!r3.has_value() && r3.error_or(error(0)).code == 11,
        "escape transits the abi::__forced_unwind idiom and reaches the landing");
  check(!g_idiom_real_entered, "the real-exception arm of the idiom was not entered");
  check(g_ctor == g_dtor, "guards balanced across the idiom transit");
#endif
}

// Payload boundary: an error value carried by an eject, including an adversarial one,
// reaches the landing as a defined error rather than undefined behavior.
void test_payload_boundary() {
  const int probes[] = {0, -1, 1, 2147483647, -2147483648, 0x0BADC0DE};
  for (int v : probes) {
    auto r = unwind::escape_scope<error>(
        [v]() -> int { return unwind::eject(error(v, v ^ 1)); });
    check(!r.has_value(), "adversarial payload still lands as a failure");
    check(r.error_or(error(0)).code == v, "payload code round-trips to the landing");
    check(r.error_or(error(0)).domain == (v ^ 1),
          "payload domain round-trips to the landing");
  }
}

}  // namespace

#endif  // JMPXX_UNWIND_AVAILABLE

int main() {
  std::printf("unwind_exec: backend available=%d\n", (int)unwind::available());
#if !JMPXX_UNWIND_AVAILABLE
  std::printf("  arm has no backend on this target; nothing to execute\n");
  return 0;
#else
  test_deep_destructor_counts();
  test_void_body();
  test_nesting();
  test_catch_all_transit();
  test_payload_boundary();
  std::printf("unwind_exec: %s\n", g_fail == 0 ? "PASS" : "FAIL");
  return g_fail == 0 ? 0 : 1;
#endif
}
