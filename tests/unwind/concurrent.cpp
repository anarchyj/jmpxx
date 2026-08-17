// SPDX-License-Identifier: MIT
// Concurrent use of the unwind arm. Every thread runs its own landing scopes, nests them,
// escapes from varying depth, and mixes success with failure, so the per-thread landing
// stack and the per-scope unwind object are exercised rather than trusted.
//
// Three properties are checked on every thread: the escape lands at the scope that owns
// it and carries the exact payload that thread ejected, every automatic on the path is
// destructed exactly once, and the thread's landing stack is empty again when the work
// ends. The payload carries the thread's identity, so a landing that receives another
// thread's error is a failure rather than a passing count.
//
// The tier is built twice, against the shipped arm and against the regressed arm in
// known_bad/ whose landing stack is one global instead of per-thread. It also runs under
// the thread sanitizer, where a race in the arm's own state is a finding.
#if JMPXX_TEST_REGRESSED_ARM
#include "known_bad/regressed_arm.hpp"
namespace arm = jmpxx_known_bad;
#else
#include "jmpxx/unwind.hpp"
namespace arm = jmpxx::unwind;
#endif

#include "jmpxx/core.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<int> g_failures{0};

void fail(const char* what, int thread_id) {
  std::printf("  FAIL [thread %d]: %s\n", thread_id, what);
  g_failures.fetch_add(1, std::memory_order_relaxed);
}

// Per-thread counters, so the fixture's own bookkeeping introduces no sharing for the
// thread sanitizer to report.
thread_local int t_ctor = 0;
thread_local int t_dtor = 0;
thread_local volatile int t_sink = 0;

struct guard {
  int id;
  explicit guard(int i) : id(i) { ++t_ctor; }
  ~guard() {
    ++t_dtor;
    t_sink = t_sink + id;
  }
};

// A second error type, so two threads can hold scopes of different error types at once
// and the type tag that routes an eject is exercised across threads.
struct tagged_error {
  int thread_id;
  int depth;
  unsigned marker;
};

// A deterministic per-thread sequence. Each thread walks its own values, so the run is
// reproducible and no thread depends on another's timing.
unsigned next_step(unsigned& state) noexcept {
  state = state * 1664525u + 1013904223u;
  return state >> 16;
}

// Descend `depth` frames, each owning a guard, then either escape or return a value.
template <class E>
int descend(int depth, bool escaping, E payload) {
  guard g{depth};
  t_sink = t_sink + depth;
  if (depth <= 0) {
    if (escaping) arm::eject(payload);
    return 0;
  }
  return descend<E>(depth - 1, escaping, payload) + 1;
}

// One thread's work: repeated escapes at varying depth, nested landings, and success
// paths, each checked for payload identity and destructor balance.
void worker(int thread_id, int iterations) {
  unsigned state = static_cast<unsigned>(thread_id) * 2654435761u + 1u;
  for (int i = 0; i < iterations; ++i) {
    const int depth = static_cast<int>(next_step(state) % 9);
    const bool escaping = (next_step(state) & 1u) != 0;
    const bool nested = (next_step(state) & 2u) != 0;
    const bool typed = (next_step(state) & 4u) != 0;

    t_ctor = t_dtor = 0;
    if (typed) {
      const tagged_error payload{thread_id, depth, 0xC0FFEEu};
      auto outer = arm::escape_scope<tagged_error>([&] {
        if (!nested) return descend<tagged_error>(depth, escaping, payload);
        // An inner landing of the same error type: an escape below it lands there and
        // the outer scope sees a value, which is the nesting contract under concurrency.
        auto inner = arm::escape_scope<tagged_error>(
            [&] { return descend<tagged_error>(depth, escaping, payload); });
        if (escaping) {
          if (inner.has_value()) fail("nested escape reached the outer scope", thread_id);
          else if (inner.error().thread_id != thread_id)
            fail("nested landing received another thread's payload", thread_id);
        }
        return 7;
      });
      if (nested) {
        if (!outer.has_value() || outer.value_or(0) != 7)
          fail("the outer scope did not return its own value", thread_id);
      } else if (escaping) {
        if (outer.has_value()) fail("an escape did not reach its landing", thread_id);
        else if (outer.error().thread_id != thread_id ||
                 outer.error().depth != depth ||
                 outer.error().marker != 0xC0FFEEu)
          fail("a landing received the wrong payload", thread_id);
      } else if (!outer.has_value()) {
        fail("a success path landed as a failure", thread_id);
      }
    } else {
      const jmpxx::error payload(1000 + thread_id, depth);
      auto r = arm::escape_scope<jmpxx::error>(
          [&] { return descend<jmpxx::error>(depth, escaping, payload); });
      if (escaping) {
        if (r.has_value()) fail("an escape did not reach its landing", thread_id);
        else if (r.error().code != 1000 + thread_id || r.error().domain != depth)
          fail("a landing received the wrong payload", thread_id);
      } else if (!r.has_value()) {
        fail("a success path landed as a failure", thread_id);
      }
    }

    if (t_ctor != t_dtor)
      fail("a destructor was skipped or double-run under concurrency", thread_id);
  }

#if !JMPXX_TEST_REGRESSED_ARM
  // The landing stack this thread pushed is empty again. A leaked entry would point at a
  // frame that no longer exists and would misroute the next escape on this thread.
  if (jmpxx::unwind::detail::active() != nullptr)
    fail("the thread's landing stack was not restored", thread_id);
#endif
}

}  // namespace

int main(int argc, char** argv) {
  int threads = 8;
  int iterations = 400;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
      threads = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
      iterations = std::atoi(argv[++i]);
  }
  if (threads < 1) threads = 1;

  // Each thread is one case; the count is what ran rather than what was asked for.
  std::printf("    cases.asked  %d\n    cases.known  %d\n", threads, threads);
  std::printf("unwind_concurrent: threads=%d iterations=%d\n", threads, iterations);
  if (!arm::escape_scope<jmpxx::error>([] { return 0; }).has_value()) {
    std::printf("  FAIL: the arm did not run a trivial scope\n");
    return 1;
  }

  std::vector<std::thread> pool;
  pool.reserve(static_cast<std::size_t>(threads));
  for (int t = 0; t < threads; ++t)
    pool.emplace_back(worker, t, iterations);
  for (auto& th : pool) th.join();

  const int failures = g_failures.load(std::memory_order_relaxed);
  std::printf("unwind_concurrent: %s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL",
              failures, failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
