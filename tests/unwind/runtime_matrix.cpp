// SPDX-License-Identifier: MIT
// One case of the arm's runtime behaviour matrix per run.
//
// How an escape interacts with a handler on its path is decided by the C++ runtime, not
// by the arm, and the runtimes disagree: a catch-all consumes a foreign forced unwind on
// libstdc++ and libc++abi, transits it on libcxxrt, and on WebAssembly the escape is an
// ordinary throw with no forced-unwind exemption at all. A noexcept frame on the path
// terminates everywhere. The arm's contract is therefore stated per runtime, and this
// fixture is how each runtime's answer is obtained rather than assumed.
//
// Each case prints one outcome token and exits. A case whose declared outcome is a
// defined termination kills this process instead, which the driver observes; that is why
// one case runs per process. The forbidden outcome, the one the arm must never produce
// on any runtime, is silent_loss: the scope returning a value the program never made.
//
// jmpxx-verify unwind-matrix drives every case and reports the table. The fixture stays
// free of anything but the C standard library so it also builds for the WebAssembly,
// bare-metal-adjacent, and MSVC cells, where the driver is a loop in the job rather than
// the harness.
#include "jmpxx/unwind.hpp"
#include "jmpxx/core.hpp"

#include <cstdio>
#include <cstring>

#if JMPXX_UNWIND_BACKEND_ITANIUM && defined(__GLIBCXX__)
#include <cxxabi.h>  // abi::__forced_unwind is a libstdc++ type
#endif

using namespace jmpxx;

// Gated on the backend for the same reason as the execution tier: where the arm refuses
// instantiation, the cases cannot be compiled, and the fixture reports the absence.
#if JMPXX_UNWIND_AVAILABLE

namespace {

bool g_inject_silent_loss = false;
int g_ctor = 0, g_dtor = 0;
volatile int g_sink = 0;

struct guard {
  int id;
  explicit guard(int i) : id(i) { ++g_ctor; }
  ~guard() {
    ++g_dtor;
    g_sink = g_sink + id;
  }
};

// The escaping leaf every case shares. Kept out of line so each case has real frames.
JMPXX_NOINLINE int leaf(int code) {
  guard g{0};
  g_sink = g_sink + code;
  if (!g_inject_silent_loss) unwind::eject(error(code, 1));
  return code;  // the injected subject returns a value where an escape was promised
}

JMPXX_NOINLINE int middle(int code) {
  guard g{1};
  g_sink = g_sink + code;
  return leaf(code) + g.id;
}

// The outcome vocabulary. A case prints exactly one of these, and the driver reads the
// last line; a case that dies prints nothing and is classified as terminated.
void report(const char* outcome, const char* detail = "") {
  std::printf("outcome=%s ctor=%d dtor=%d %s\n", outcome, g_ctor, g_dtor, detail);
  std::fflush(stdout);
}

// Classify one landing: the escape must arrive with its payload and balanced guards.
void report_landing(const result<int, error>& r, int expected_code) {
  if (r.has_value()) {
    report(g_inject_silent_loss ? "silent_loss" : "value", "the scope returned a value");
    return;
  }
  if (r.error().code != expected_code) {
    report("wrong_payload", "the landing received another escape's error");
    return;
  }
  if (g_ctor != g_dtor) {
    report("imbalance", "a destructor was skipped or double-run");
    return;
  }
  report("landed");
}

int case_no_handler() {
  auto r = unwind::escape_scope<error>([] { return middle(21); });
  report_landing(r, 21);
  return 0;
}

JMPXX_NOINLINE int typed_catch_frame() {
  guard g{2};
  try {
    return middle(22);
  } catch (int) {
    report("consumed", "a typed catch consumed the escape");
    return -1;
  }
}

int case_typed_catch() {
  auto r = unwind::escape_scope<error>([] { return typed_catch_frame(); });
  report_landing(r, 22);
  return 0;
}

JMPXX_NOINLINE int catch_all_rethrow_frame() {
  guard g{3};
  try {
    return middle(23);
  } catch (...) {
    throw;  // cooperative: re-raise whatever is in flight
  }
}

int case_catch_all_rethrow() {
  auto r = unwind::escape_scope<error>([] { return catch_all_rethrow_frame(); });
  report_landing(r, 23);
  return 0;
}

JMPXX_NOINLINE int catch_all_swallow_frame() {
  guard g{4};
  try {
    return middle(24);
  } catch (...) {
    return 0;  // non-cooperative: the escape is consumed here
  }
}

int case_catch_all_swallow() {
  auto r = unwind::escape_scope<error>([] { return catch_all_swallow_frame(); });
  report_landing(r, 24);
  return 0;
}

#if JMPXX_UNWIND_BACKEND_ITANIUM && defined(__GLIBCXX__)
JMPXX_NOINLINE int forced_unwind_idiom_frame() {
  guard g{5};
  try {
    return middle(25);
  } catch (const abi::__forced_unwind&) {
    throw;  // re-raise only the forced unwind
  } catch (...) {
    report("consumed", "the real-exception arm of the idiom ran");
    return -1;
  }
}
#endif

int case_forced_unwind_idiom() {
#if JMPXX_UNWIND_BACKEND_ITANIUM && defined(__GLIBCXX__)
  auto r = unwind::escape_scope<error>([] { return forced_unwind_idiom_frame(); });
  report_landing(r, 25);
#else
  report("unavailable", "this runtime has no forced-unwind type to catch");
#endif
  return 0;
}

JMPXX_NOINLINE int noexcept_frame() noexcept {
  guard g{6};
  return middle(26);
}

int case_noexcept_barrier() {
  auto r = unwind::escape_scope<error>([] { return noexcept_frame(); });
  report_landing(r, 26);
  return 0;
}

// A noexcept function that holds the landing rather than sitting on the escape path. The
// unwind stops at the landing, so it never crosses this frame's barrier.
JMPXX_NOINLINE int noexcept_landing_holder() noexcept {
  auto r = unwind::escape_scope<error>([] { return middle(27); });
  return r.has_value() ? 0 : r.error().code;
}

int case_noexcept_landing() {
  const int code = noexcept_landing_holder();
  if (code != 27) {
    report("wrong_payload", "a landing inside a noexcept function lost its escape");
    return 0;
  }
  if (g_ctor != g_dtor) {
    report("imbalance", "a destructor was skipped or double-run");
    return 0;
  }
  report("landed");
  return 0;
}

// A cleanup that raises and handles its own exception while the escape is unwinding. The
// language allows this for an ordinary unwind as long as nothing escapes the destructor;
// whether a runtime allows it during a forced unwind is a per-runtime answer.
struct throwing_cleanup {
  ~throwing_cleanup() {
    ++g_dtor;
    try {
      throw 5;
    } catch (int) {
      g_sink = g_sink + 1;
    }
  }
};

int case_cleanup_throws_and_catches() {
  auto r = unwind::escape_scope<error>([]() -> int {
    throwing_cleanup local;
    ++g_ctor;
    return middle(28);
  });
  report_landing(r, 28);
  return 0;
}

// A cleanup that opens its own landing and escapes inside it while the outer escape is
// still unwinding. Supported by the arm, and per-runtime because it puts two unwinds in
// flight on one thread.
bool g_inner_landed = false;
struct nesting_cleanup {
  ~nesting_cleanup() {
    ++g_dtor;
    auto r = unwind::escape_scope<error>([]() -> int { return middle(99); });
    g_inner_landed = !r.has_value() && r.error().code == 99;
  }
};

int case_nested_escape_in_cleanup() {
  auto r = unwind::escape_scope<error>([]() -> int {
    nesting_cleanup nest;
    ++g_ctor;
    return middle(29);
  });
  if (!g_inner_landed && !r.has_value()) {
    report("inner_lost", "the nested escape did not reach its own landing");
    return 0;
  }
  report_landing(r, 29);
  return 0;
}

// A cleanup that escapes to the scope whose escape is already unwinding. The arm refuses
// this; reaching the report below means it did not.
struct reejecting_cleanup {
  ~reejecting_cleanup() {
    ++g_dtor;
    if (!g_inject_silent_loss) unwind::eject(error(31));
  }
};

int case_reeject_in_cleanup() {
  auto r = unwind::escape_scope<error>([]() -> int {
    reejecting_cleanup again;
    ++g_ctor;
    return middle(30);
  });
  report("not_refused", r.has_value() ? "the scope returned a value"
                                      : "the scope landed instead of refusing");
  return 0;
}

struct matrix_case {
  const char* name;
  int (*run)();
};

const matrix_case cases[] = {
    {"no_handler", case_no_handler},
    {"typed_catch", case_typed_catch},
    {"catch_all_rethrow", case_catch_all_rethrow},
    {"catch_all_swallow", case_catch_all_swallow},
    {"forced_unwind_idiom", case_forced_unwind_idiom},
    {"noexcept_barrier", case_noexcept_barrier},
    {"noexcept_landing", case_noexcept_landing},
    {"cleanup_throws_and_catches", case_cleanup_throws_and_catches},
    {"nested_escape_in_cleanup", case_nested_escape_in_cleanup},
    {"reeject_in_cleanup", case_reeject_in_cleanup},
};

int run_named(const char* wanted) {
  for (const auto& c : cases)
    if (std::strcmp(c.name, wanted) == 0) return c.run();
  std::fprintf(stderr, "unknown case: %s\n", wanted);
  return 2;
}

}  // namespace

#else  // JMPXX_UNWIND_AVAILABLE

namespace {

bool g_inject_silent_loss = false;

// A target whose backend the arm refuses cannot compile the cases at all, so the same
// case names answer "unavailable" here. The driver then asks every target the same
// questions and the row reads honestly rather than going missing.
struct matrix_case {
  const char* name;
  int (*run)();
};

int report_unavailable() {
  std::printf("outcome=unavailable ctor=0 dtor=0 the arm has no backend on this target\n");
  return 0;
}

const matrix_case cases[] = {
    {"no_handler", report_unavailable},
    {"typed_catch", report_unavailable},
    {"catch_all_rethrow", report_unavailable},
    {"catch_all_swallow", report_unavailable},
    {"forced_unwind_idiom", report_unavailable},
    {"noexcept_barrier", report_unavailable},
    {"noexcept_landing", report_unavailable},
    {"cleanup_throws_and_catches", report_unavailable},
    {"nested_escape_in_cleanup", report_unavailable},
    {"reeject_in_cleanup", report_unavailable},
};

int run_named(const char* wanted) {
  for (const auto& c : cases)
    if (std::strcmp(c.name, wanted) == 0) return c.run();
  std::fprintf(stderr, "unknown case: %s\n", wanted);
  return 2;
}

}  // namespace

#endif  // JMPXX_UNWIND_AVAILABLE

namespace {

// The runtime this build exercises, reported so a matrix row names the C++ runtime that
// produced it rather than only the operating system.
const char* runtime_name() {
#if defined(__GLIBCXX__)
  return "libstdc++";
#elif defined(_LIBCPP_VERSION) && defined(__FreeBSD__)
  return "libc++/libcxxrt";
#elif defined(_LIBCPP_VERSION)
  return "libc++/libc++abi";
#elif defined(_MSVC_STL_VERSION) || JMPXX_COMPILER_MSVC
  return "msvc-stl";
#else
  return "unknown";
#endif
}

}  // namespace

// The case is selected by name on the command line, except where the toolchain cannot
// deliver one. Emscripten 3.1.6 produces no output at all from a main that takes argc and
// argv, so the WebAssembly cell bakes the case in at compile time and runs one case per
// build. The cases themselves are identical either way.
#ifdef JMPXX_MATRIX_CASE

int main() { return run_named(JMPXX_MATRIX_CASE); }

#else

int main(int argc, char** argv) {
  const char* wanted = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--inject-silent-loss") == 0)
      g_inject_silent_loss = true;
    else if (std::strcmp(argv[i], "--list") == 0) {
      for (const auto& c : cases) std::printf("%s\n", c.name);
      return 0;
    } else if (std::strcmp(argv[i], "--runtime") == 0) {
      std::printf("%s\n", runtime_name());
      return 0;
    } else if (std::strcmp(argv[i], "--case") == 0 && i + 1 < argc) {
      wanted = argv[++i];
    } else {
      wanted = argv[i];
    }
  }
  if (!wanted) {
    std::fprintf(stderr,
                 "usage: unwind_runtime_matrix --case <name> [--inject-silent-loss]\n"
                 "       unwind_runtime_matrix --list | --runtime\n");
    return 2;
  }
  return run_named(wanted);
}

#endif  // JMPXX_MATRIX_CASE
