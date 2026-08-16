// SPDX-License-Identifier: MIT
// The arm's reentrancy contract, one case per run so a case that ends in a defined
// termination is observed as the death of this process rather than swallowed.
//
// Code that runs as a cleanup during an escape may open its own landing scope, and may
// not escape while that escape is still unwinding. Both shapes of a second escape are
// refused with a diagnostic, the one aimed at the scope already unwinding and the one
// aimed at a fresh scope the cleanup opened, because the second is supported only on the
// DWARF ABIs and terminates on the ARM exception-handling ABI and on WebAssembly. The
// refusal is what makes the contract the same everywhere.
//
// The remaining cases are the other ways an eject can be misdirected, each of which must
// be a defined termination and not a landing in the wrong place: no scope on this
// thread, an error type the active scope does not receive, a scope that has already
// returned, and a scope that belongs to another thread.
//
// tests/scripts/unwind_reentrancy_check.sh holds the expected outcome of each case and
// fails if one lands where it must terminate or terminates where it must land.
#include "jmpxx/unwind.hpp"
#include "jmpxx/core.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace jmpxx;

namespace {

// With --fake-success every case that must terminate returns success instead. It is the
// inverted subject for the driver: a driver that still passes has no teeth.
bool g_fake_success = false;

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

JMPXX_NOINLINE int descend(int depth, int code) {
  guard g{depth};
  g_sink = g_sink + depth;
  if (depth <= 0) unwind::eject(error(code, depth));
  return descend(depth - 1, code) + 1;
}

// A cleanup that opens its own landing scope and escapes inside it while the outer
// escape is unwinding. Opening the scope is allowed; escaping from it is refused, so
// only the case whose inner body returns normally survives. What the cleanup observed is
// recorded at file scope, because the cleanup's own object is in a frame the outer escape
// destroys and nothing may read it afterwards.
bool g_inner_landed = false;
int g_inner_code = -1;

struct nesting_cleanup {
  bool inner_escapes = true;
  ~nesting_cleanup() {
    ++g_dtor;
    auto r = unwind::escape_scope<error>([this]() -> int {
      if (inner_escapes) return descend(3, 4242);
      return 7;
    });
    g_inner_landed = !r.has_value();
    g_inner_code = r.has_value() ? r.value() : r.error().code;
  }
};

int case_nested_scope_in_cleanup() {
  auto outer = unwind::escape_scope<error>([]() -> int {
    nesting_cleanup nest;
    guard g{99};
    ++g_ctor;  // the cleanup counts itself in its destructor
    return descend(4, 11);
  });
  // Reached only if the arm let a cleanup escape while an escape was already unwinding.
  std::printf("nested_scope_in_cleanup: outer=%d inner_landed=%d inner_code=%d "
              "ctor=%d dtor=%d\n",
              outer.has_value() ? 0 : outer.error().code,
              static_cast<int>(g_inner_landed), g_inner_code, g_ctor, g_dtor);
  return 0;
}

int case_nested_scope_success_in_cleanup() {
  auto outer = unwind::escape_scope<error>([]() -> int {
    nesting_cleanup nest;
    nest.inner_escapes = false;
    ++g_ctor;
    return descend(2, 12);
  });
  const bool ok = !outer.has_value() && outer.error().code == 12 && !g_inner_landed &&
                  g_inner_code == 7 && g_ctor == g_dtor;
  std::printf("nested_scope_success_in_cleanup: outer=%d inner_value=%d ctor=%d dtor=%d\n",
              outer.has_value() ? 0 : outer.error().code, g_inner_code, g_ctor, g_dtor);
  return ok ? 0 : 1;
}

// A cleanup that ejects to the scope whose escape is already unwinding.
struct reejecting_cleanup {
  ~reejecting_cleanup() {
    ++g_dtor;
    if (g_fake_success) return;
    unwind::eject(error(777));
  }
};

int case_reeject_in_cleanup() {
  auto outer = unwind::escape_scope<error>([]() -> int {
    reejecting_cleanup again;
    ++g_ctor;
    return descend(3, 13);
  });
  // Reached only if the arm did not refuse the second escape.
  std::printf("reeject_in_cleanup: returned has_value=%d code=%d\n",
              static_cast<int>(outer.has_value()),
              outer.has_value() ? 0 : outer.error().code);
  return 0;
}

int case_no_active_scope() {
  if (g_fake_success) {
    std::printf("no_active_scope: skipped\n");
    return 0;
  }
  unwind::eject(error(1));
  std::printf("no_active_scope: eject returned\n");
  return 0;
}

int case_type_mismatch() {
  struct other_error {
    int value;
  };
  auto r = unwind::escape_scope<error>([]() -> int {
    if (g_fake_success) return 0;
    unwind::eject(other_error{5});
    return 0;
  });
  std::printf("type_mismatch: returned has_value=%d\n", static_cast<int>(r.has_value()));
  return 0;
}

int case_after_scope_returned() {
  auto r = unwind::escape_scope<error>([] { return 3; });
  std::printf("after_scope_returned: scope value=%d\n", r.value_or(-1));
  if (g_fake_success) return 0;
  unwind::eject(error(2));
  return 0;
}

int case_other_thread_has_the_scope() {
  std::atomic<bool> scope_open{false};
  std::atomic<bool> release{false};
  std::thread holder([&] {
    auto r = unwind::escape_scope<error>([&]() -> int {
      scope_open.store(true, std::memory_order_release);
      while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
      return 5;
    });
    (void)r.value_or(0);
  });
  while (!scope_open.load(std::memory_order_acquire)) std::this_thread::yield();
  // This thread has no scope of its own; the other thread's scope must not receive it.
  if (!g_fake_success) unwind::eject(error(3));
  release.store(true, std::memory_order_release);
  holder.join();
  std::printf("other_thread_has_the_scope: eject returned on a thread with no scope\n");
  return 0;
}

struct case_entry {
  const char* name;
  int (*run)();
};

const case_entry cases[] = {
    {"nested_scope_in_cleanup", case_nested_scope_in_cleanup},
    {"nested_scope_success_in_cleanup", case_nested_scope_success_in_cleanup},
    {"reeject_in_cleanup", case_reeject_in_cleanup},
    {"no_active_scope", case_no_active_scope},
    {"type_mismatch", case_type_mismatch},
    {"after_scope_returned", case_after_scope_returned},
    {"other_thread_has_the_scope", case_other_thread_has_the_scope},
};

}  // namespace

int main(int argc, char** argv) {
  const char* wanted = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--fake-success") == 0)
      g_fake_success = true;
    else if (std::strcmp(argv[i], "--list") == 0) {
      for (const auto& c : cases) std::printf("%s\n", c.name);
      return 0;
    } else {
      wanted = argv[i];
    }
  }
  if (!wanted) {
    std::fprintf(stderr, "usage: unwind_reentrancy <case> [--fake-success] | --list\n");
    return 2;
  }
  for (const auto& c : cases)
    if (std::strcmp(c.name, wanted) == 0) return c.run();
  std::fprintf(stderr, "unknown case: %s\n", wanted);
  return 2;
}
