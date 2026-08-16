// SPDX-License-Identifier: MIT
// The harness's unwind-arm probes: cost, per-runtime behaviour, long-run stress, and
// thread scaling. Kept in their own unit because the arm is the one capability whose
// correctness rests on the platform runtime, so observing it takes more than a single
// probe and none of it belongs to the portable surface's probes.
//
// Each probe is preceded by the fixtures it drives, in an unnamed namespace so the escape
// chains, counters, and per-thread state stay local to this unit and cannot be reached
// from the rest of the harness.
#include "unwind_probes.hpp"

#include "jmpxx/core.hpp"
#include "jmpxx/unwind.hpp"
#include "support.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace jmpxx;

namespace jv {
namespace {

// unwind: drive the experimental non-local unwind arm and report what only execution
// can show: the destructor count over a deep escape and the sad-path latency
// distribution. The destructor count shows the arm runs every cleanup on the path;
// the distribution, reported by median and high percentiles, is the evidence behind
// the bounded-sad-path claim. A C++ throw at the same depth is measured alongside so
// the comparison is measured rather than asserted: the arm's sad path is bounded, not
// uniformly faster than a throw.
//
// The gate compares the arm's 99th-percentile latency to a C++ throw's at the same depth.
// An absolute nanosecond bound would not travel across machines, and an absolute
// percentile-to-median ratio flakes on a shared runner, where a rare scheduling stall
// inflates the high percentile of any operation. Measuring both against the same runner
// cancels that noise: a stall that lengthens the arm's tail lengthens the throw's tail
// too, so the ratio between them stays bounded. The --inject-jitter self-test makes
// the arm's cleanup path, and not the throw's, non-deterministic, which drives the ratio
// past the bound and fails the gate's own negative path.
namespace uw {

volatile long long g_sink = 0;
bool g_jitter = false;
long long g_jitter_counter = 0;
constexpr int depth = 8;  // at least eight frames on the escape path

// A guard whose destructor does real work. The Jitter parameter selects whether a known
// non-deterministic cleanup may be injected. It is injected on the forced-unwind path
// only, so the gate that compares the arm's tail latency to a C++ throw's catches it,
// while ordinary scheduling noise affects both paths together and does not.
template <bool Jitter>
struct GuardT {
  int v;
  ~GuardT() {
    g_sink = g_sink + v;
    // Inject jitter on one cleanup per escape, the leaf (v == 0), rather than once per
    // frame, so the cost is one long cleanup per affected escape. A fraction of escapes
    // then become far costlier than the rest, the non-determinism the gate catches. The
    // loop dwarfs an ordinary escape on any runner, including a slow one with a high
    // baseline, so the injected ratio clears the bound by a wide margin.
    if (Jitter && g_jitter && v == 0 && (g_jitter_counter++ & 31) == 0) {
      volatile long long s = 0;
      for (int i = 0; i < 500000; ++i) s = s + i;
      g_sink = g_sink + s;
    }
  }
};

// The escape chains are template recursions, so the depth is a compile-time bound that
// expands to distinct frames rather than a runtime self-recursion the compiler cannot
// fold into a single terminating path. The forced-unwind chain carries the jittered
// guard; the C++ throw chain, the baseline, carries the plain one.
template <int D>
int fu_chain() {
  GuardT<true> g{D};
  if constexpr (D <= 0)
    return jmpxx::unwind::eject(jmpxx::error(42));
  else
    return fu_chain<D - 1>() + g.v;
}
int fu_escape() {
  auto r = jmpxx::unwind::escape_scope<jmpxx::error>([] { return fu_chain<depth>(); });
  return r.has_value() ? 0 : r.error().code;
}

struct ex {
  int code;
};
template <int D>
int th_chain() {
  GuardT<false> g{D};
  if constexpr (D <= 0)
    throw ex{42};
  else
    return th_chain<D - 1>() + g.v;
}
int th_escape() {
  try {
    return th_chain<depth>();
  } catch (const ex& e) {
    return e.code;
  }
}

struct dist {
  long long median, p90, p99, max;
};

dist summarize(std::vector<long long>& ns) {
  std::sort(ns.begin(), ns.end());
  auto at = [&](double p) { return ns[static_cast<std::size_t>(p * (ns.size() - 1))]; };
  return {at(0.5), at(0.9), at(0.99), ns.back()};
}

// Measure two escape paths back to back in one loop so they sample the same contention.
// The gate compares their tails, so the measurement must expose both paths to the same
// stalls. Timing them microseconds apart in each iteration, rather than in two separate
// loops, makes a stall that spans an iteration inflate both samples, which keeps the
// relative bound from flaking when a shared runner is briefly busy.
template <class A, class B>
std::pair<dist, dist> measure_pair(A fa, B fb, int iters) {
  for (int i = 0; i < 2000; ++i) g_sink = g_sink + fa() + fb();  // warm up both
  std::vector<long long> na, nb;
  na.reserve(iters);
  nb.reserve(iters);
  using clock = std::chrono::steady_clock;
  auto elapsed = [](clock::time_point s, clock::time_point e) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
  };
  for (int i = 0; i < iters; ++i) {
    auto t0 = clock::now();
    g_sink = g_sink + fa();
    auto t1 = clock::now();
    g_sink = g_sink + fb();
    auto t2 = clock::now();
    na.push_back(elapsed(t0, t1));
    nb.push_back(elapsed(t1, t2));
  }
  return {summarize(na), summarize(nb)};
}

// Count constructions and destructions over a deep escape, checking that the arm runs every
// cleanup exactly once. Kept apart from the timed guard so timing carries no counters.
long long c_ctor = 0, c_dtor = 0;
struct Counted {
  Counted() { ++c_ctor; }
  ~Counted() { ++c_dtor; }
};
template <int D>
int cnt_chain() {
  Counted g;
  if constexpr (D <= 0)
    return jmpxx::unwind::eject(jmpxx::error(7));
  else
    return cnt_chain<D - 1>();
}

}  // namespace uw

}  // namespace

int probe_unwind(Fmt fmt, const std::vector<std::string>& args) {
  int iters = 50000;
  // The committed determinism bound: the arm's 99th-percentile sad path stays within this
  // multiple of a C++ throw's at the same depth. It is set generously above the observed
  // ratio (near 1.1 to 1.9 across compilers) so ordinary scheduling noise on a shared
  // runner does not flake it, while still well below the ratio an injected
  // non-deterministic cleanup produces, so the inverted self-test still fails.
  double bound = 3.0;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto next = [&]() { return (i + 1 < args.size()) ? args[++i] : std::string(); };
    if (a == "--inject-jitter") uw::g_jitter = true;
    else if (a == "--iters") iters = std::atoi(next().c_str());
    else if (a == "--bound-factor") bound = std::atof(next().c_str());
  }
  Report r(fmt, "unwind");
  r.boolean("available", unwind::available());
  if (!unwind::available()) {
    r.note("the unwind arm has no backend on this target; nothing to drive");
    return r.finish();
  }
#if JMPXX_UNWIND_BACKEND_ITANIUM
  r.str("backend", "itanium");
#elif JMPXX_UNWIND_BACKEND_WASM
  r.str("backend", "wasm");
#elif JMPXX_UNWIND_BACKEND_SEH
  r.str("backend", "seh");
#endif

  // Destructor count over a deep escape: every cleanup runs exactly once.
  uw::c_ctor = uw::c_dtor = 0;
  auto cr = unwind::escape_scope<error>([] { return uw::cnt_chain<uw::depth>(); });
  r.num("escape.frames", uw::depth + 1);
  r.num("escape.constructed", uw::c_ctor);
  r.num("escape.destructed", uw::c_dtor);
  r.boolean("escape.balanced",
            uw::c_ctor == uw::c_dtor && uw::c_ctor == uw::depth + 1);
  if (cr.has_value() || cr.error().code != 7)
    r.fail("the escape did not deliver its error to the landing");
  if (uw::c_ctor != uw::c_dtor || uw::c_ctor != uw::depth + 1)
    r.fail("a destructor was skipped or double-run on the escape path");

  // Sad-path distribution, forced unwind against a C++ throw at the same depth. The gate
  // compares the arm's tail to the throw's, so shared-runner scheduling noise that
  // inflates both tails together cancels rather than flaking the gate.
  auto [fu, th] = uw::measure_pair(uw::fu_escape, uw::th_escape, iters);
  double ratio =
      static_cast<double>(fu.p99) / static_cast<double>(th.p99 ? th.p99 : 1);
  r.num("sad_path.iters", iters);
  r.num("sad_path.median_ns", fu.median);
  r.num("sad_path.p90_ns", fu.p90);
  r.num("sad_path.p99_ns", fu.p99);
  r.num("sad_path.max_ns", fu.max);
  r.num("cxx_throw.median_ns", th.median);
  r.num("cxx_throw.p99_ns", th.p99);
  r.num("sad_path.p99_vs_throw_p99_x100", static_cast<long long>(ratio * 100));
  r.num("sad_path.bound_x100", static_cast<long long>(bound * 100));
  r.boolean("sad_path.bounded", ratio <= bound);
  r.note(uw::g_jitter ? "jitter injected: a non-deterministic cleanup is expected to "
                        "exceed the bound"
                      : "the sad path is bounded; its tail is comparable to a C++ throw's, "
                        "not uniformly faster");
  if (ratio > bound)
    r.fail("the sad-path p99 exceeded the committed multiple of a C++ throw's p99");
  return r.finish();
}

namespace {

// unwind-matrix: report what each C++ runtime does with an escape that meets a handler
// or a barrier on its path. The runtimes disagree, so the arm's contract is per runtime
// and has to be obtained rather than assumed. Each case runs as its own process, because
// a case whose declared outcome is a defined termination kills the process that runs it,
// and the driver classifies what came back.
namespace uwm {

// One row per case: the outcomes the arm treats as safe on any runtime. An outcome
// outside its row fails the probe. The forbidden ones appear in no row: a fabricated
// value, another escape's payload, an unbalanced cleanup, or a handler quietly consuming
// the escape.
struct expectation {
  const char* name;
  const char* allowed;
};

const expectation expectations[] = {
    {"no_handler", "landed"},
    {"typed_catch", "landed"},
    {"catch_all_rethrow", "landed terminated"},
    {"catch_all_swallow", "landed terminated"},
    {"forced_unwind_idiom", "landed terminated unavailable"},
    {"noexcept_barrier", "terminated"},
    {"noexcept_landing", "landed"},
    {"cleanup_throws_and_catches", "landed terminated"},
    {"nested_escape_in_cleanup", "terminated"},
    {"reeject_in_cleanup", "terminated"},
};

bool allows(const char* allowed, const std::string& outcome) {
  std::istringstream in(allowed);
  for (std::string token; in >> token;)
    if (token == outcome) return true;
  return false;
}

// Run one case and name what came back. A case that reports nothing did not survive to
// report, which is the termination outcome.
std::string run_case(const std::string& fixture, const std::string& name, bool inject,
                     const std::string& tmp) {
  // Several cases end in a defined termination by design. The child's core limit is
  // zeroed and its debug-info fetching disabled first, because a crash image per
  // terminating case and a network round trip per stack frame cost far more than the
  // exit status this reads. tests/scripts/crash_hygiene.sh states the same rule for the
  // shell-driven tiers.
  std::string cmd = "ulimit -c 0 2>/dev/null; DEBUGINFOD_URLS= " + fixture +
                    " --case " + name;
  if (inject) cmd += " --inject-silent-loss";
  cmd += " > " + tmp + " 2>&1";
  (void)std::system(cmd.c_str());
  const std::string out = read_file(tmp);
  const std::size_t at = out.rfind("outcome=");
  if (at == std::string::npos) return "terminated";
  const std::size_t start = at + 8;
  const std::size_t end = out.find_first_of(" \n", start);
  return out.substr(start, end == std::string::npos ? end : end - start);
}

// Ask the fixture a question and return everything it answered.
std::string run_query(const std::string& fixture, const std::string& flag,
                      const std::string& tmp) {
  (void)std::system((fixture + " " + flag + " > " + tmp + " 2>/dev/null").c_str());
  return read_file(tmp);
}

// The same, for a question with a one-line answer.
std::string run_query_line(const std::string& fixture, const std::string& flag,
                           const std::string& tmp) {
  const std::string out = run_query(fixture, flag, tmp);
  return out.substr(0, out.find('\n'));
}

}  // namespace uwm

}  // namespace

int probe_unwind_matrix(Fmt fmt, const std::vector<std::string>& args) {
  std::string fixture;
  bool inject = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--fixture" && i + 1 < args.size()) fixture = args[++i];
    else if (args[i] == "--inject-silent-loss") inject = true;
  }
  Report r(fmt, "unwind-matrix");
  if (fixture.empty()) {
    r.fail("unwind-matrix requires --fixture <runtime-matrix binary>");
    return r.finish();
  }

  const std::string tmp = "/tmp/jmpxx_unwind_matrix.out";
  r.str("runtime", uwm::run_query_line(fixture, "--runtime", tmp));
  std::istringstream listed(uwm::run_query(fixture, "--list", tmp));
  std::vector<std::string> names;
  for (std::string line; std::getline(listed, line);) {
    line = trim(line);
    if (!line.empty()) names.push_back(line);
  }
  // How many cases were asked, and how many the matrix knows about. A probe that quietly
  // asks fewer questions than it should still passes every case it did ask, so the count
  // is reported and a short list fails. This is not hypothetical: trimming the helper that
  // reads the fixture's answers reduced the matrix to one case without turning it red.
  const std::size_t known = sizeof(uwm::expectations) / sizeof(uwm::expectations[0]);
  r.num("cases.asked", static_cast<long long>(names.size()));
  r.num("cases.known", static_cast<long long>(known));
  if (names.size() != known)
    r.fail("the fixture listed " + std::to_string(names.size()) + " cases and the matrix "
           "expects " + std::to_string(known));

  for (const std::string& name : names) {
    const std::string outcome = uwm::run_case(fixture, name, inject, tmp);
    r.str("case." + name, outcome);
    const uwm::expectation* row = nullptr;
    for (const auto& e : uwm::expectations)
      if (name == e.name) row = &e;
    if (!row) {
      r.fail("the fixture ran a case the matrix has no expectation for: " + name);
      continue;
    }
    if (!uwm::allows(row->allowed, outcome))
      r.fail(name + ": outcome '" + outcome + "' is outside the safe set (" +
             row->allowed + ")");
  }
  r.note("each row is this runtime's answer, not a portable guarantee; the reference "
         "states the per-runtime contract");
  return r.finish();
}

namespace {

// unwind-stress: a long campaign of randomized escapes across threads, looking for the
// rare state leak, wrong landing, or cleanup imbalance that a handful of fixtures cannot
// reach. Every iteration checks that the escape it made is the one that landed and that
// the frames it built were destroyed exactly once.
namespace uws {

bool g_inject_imbalance = false;

thread_local long long t_ctor = 0;
thread_local long long t_dtor = 0;
thread_local volatile long long t_sink = 0;

struct guard {
  int id;
  explicit guard(int i) : id(i) { ++t_ctor; }
  ~guard() {
    // The injected defect skips one frame's accounting, which is what a skipped
    // destructor looks like to this probe. It proves the balance check is live.
    if (!(g_inject_imbalance && id == 3)) ++t_dtor;
    t_sink = t_sink + id;
  }
};

JMPXX_NOINLINE int descend(int depth, bool escaping, error payload) {
  guard g{depth};
  t_sink = t_sink + depth;
  if (depth <= 0) {
    if (escaping) unwind::eject(payload);
    return 0;
  }
  return descend(depth - 1, escaping, payload) + 1;
}

unsigned step(unsigned& state) noexcept {
  state = state * 1664525u + 1013904223u;
  return state >> 16;
}

struct stats {
  long long iterations = 0, escapes = 0, successes = 0, nested = 0;
  long long constructed = 0, destructed = 0;
  long long wrong_payload = 0, imbalance = 0, leaked_scope = 0, deepest = 0;
};

void merge(stats& into, const stats& from) {
  into.iterations += from.iterations;
  into.escapes += from.escapes;
  into.successes += from.successes;
  into.nested += from.nested;
  into.constructed += from.constructed;
  into.destructed += from.destructed;
  into.wrong_payload += from.wrong_payload;
  into.imbalance += from.imbalance;
  into.leaked_scope += from.leaked_scope;
  if (from.deepest > into.deepest) into.deepest = from.deepest;
}

// One escape at the requested depth and nesting, checked end to end.
void one_iteration(int thread_id, long long index, unsigned& state, stats& s) {
  const int depth = static_cast<int>(step(state) % 16);
  const bool escaping = (step(state) & 1u) != 0;
  const int nesting = static_cast<int>(step(state) % 3);
  const error payload(100000 + thread_id * 1000 + static_cast<int>(index % 1000), depth);

  t_ctor = t_dtor = 0;
  auto body = [&]() -> int { return descend(depth, escaping, payload); };
  result<int, error> r = unwind::escape_scope<error>([&]() -> int {
    if (nesting == 0) return body();
    // Nested landings: the escape lands at the innermost one, so the outer scopes see a
    // value even on the escaping path.
    auto inner = unwind::escape_scope<error>([&]() -> int {
      if (nesting == 1) return body();
      auto innermost = unwind::escape_scope<error>(body);
      return innermost.has_value() ? innermost.value() : innermost.error().code;
    });
    return inner.has_value() ? inner.value() : inner.error().code;
  });

  ++s.iterations;
  s.nested += (nesting != 0) ? 1 : 0;
  s.constructed += t_ctor;
  s.destructed += t_dtor;
  if (depth > s.deepest) s.deepest = depth;
  if (t_ctor != t_dtor) ++s.imbalance;

  if (escaping && nesting == 0) {
    ++s.escapes;
    if (r.has_value() || r.error().code != payload.code || r.error().domain != depth)
      ++s.wrong_payload;
  } else if (escaping) {
    // The innermost scope caught it, so the outer scope carries the code as a value.
    ++s.escapes;
    if (!r.has_value() || r.value() != payload.code) ++s.wrong_payload;
  } else {
    ++s.successes;
    if (!r.has_value()) ++s.wrong_payload;
  }
}

stats run_thread(int thread_id, long long iterations, int seeds) {
  stats s;
  for (int seed = 0; seed < seeds; ++seed) {
    unsigned state = static_cast<unsigned>(thread_id * 7919 + seed * 104729 + 1);
    for (long long i = 0; i < iterations; ++i) one_iteration(thread_id, i, state, s);
  }
  if (unwind::detail::active() != nullptr) ++s.leaked_scope;
  return s;
}

}  // namespace uws

}  // namespace

int probe_unwind_stress(Fmt fmt, const std::vector<std::string>& args) {
  long long iterations = 100000;
  int threads = 4;
  int seeds = 3;
  for (std::size_t i = 0; i < args.size(); ++i) {
    auto next = [&]() { return (i + 1 < args.size()) ? args[++i] : std::string(); };
    if (args[i] == "--iterations") iterations = std::atoll(next().c_str());
    else if (args[i] == "--threads") threads = std::atoi(next().c_str());
    else if (args[i] == "--seeds") seeds = std::atoi(next().c_str());
    else if (args[i] == "--inject-imbalance") uws::g_inject_imbalance = true;
  }
  if (threads < 1) threads = 1;
  if (seeds < 1) seeds = 1;

  Report r(fmt, "unwind-stress");
  if (!unwind::available()) {
    r.note("the unwind arm has no backend on this target; nothing to drive");
    return r.finish();
  }
  // The requested total is spread over the threads and seeds, so a caller asks for a
  // campaign size rather than a per-thread count.
  const long long per_thread =
      (iterations + static_cast<long long>(threads) * seeds - 1) /
      (static_cast<long long>(threads) * seeds);

  const auto started = std::chrono::steady_clock::now();
  uws::stats total;
  std::vector<uws::stats> per(static_cast<std::size_t>(threads));
  std::vector<std::thread> pool;
  pool.reserve(static_cast<std::size_t>(threads));
  for (int t = 0; t < threads; ++t)
    pool.emplace_back([&, t] { per[static_cast<std::size_t>(t)] = uws::run_thread(t, per_thread, seeds); });
  for (auto& th : pool) th.join();
  for (const auto& s : per) uws::merge(total, s);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();

  r.num("threads", threads);
  r.num("seeds", seeds);
  r.num("iterations", total.iterations);
  r.num("escapes", total.escapes);
  r.num("successes", total.successes);
  r.num("nested_scopes", total.nested);
  r.num("deepest_frame_count", total.deepest);
  r.num("constructed", total.constructed);
  r.num("destructed", total.destructed);
  r.num("wrong_payload", total.wrong_payload);
  r.num("cleanup_imbalance", total.imbalance);
  r.num("leaked_landing_stack", total.leaked_scope);
  r.num("elapsed_ms", elapsed);
  if (total.wrong_payload) r.fail("an escape landed with the wrong payload");
  if (total.imbalance) r.fail("a destructor was skipped or double-run during the campaign");
  if (total.leaked_scope) r.fail("a thread finished with a landing scope still active");
  if (total.escapes + total.successes != total.iterations)
    r.fail("the campaign did not account for every iteration");
  return r.finish();
}

namespace {

// unwind-scale: how the escape behaves when many threads escape at once. The unwinder
// walk is shared machinery, so this is really a question about the platform: the arm and
// a C++ throw take the same path through it, and the gate holds the arm to the throw's
// scaling rather than to an absolute number that would not travel between machines.
namespace uwsc {

bool g_serialize = false;
std::mutex g_lock;
thread_local volatile long long t_sink = 0;

struct guard {
  int id;
  ~guard() { t_sink = t_sink + id; }
};

constexpr int depth = 8;

template <int D>
int fu_chain() {
  guard g{D};
  if constexpr (D <= 0)
    return jmpxx::unwind::eject(jmpxx::error(42));
  else
    return fu_chain<D - 1>() + g.id;
}

int fu_escape() {
  auto r = jmpxx::unwind::escape_scope<jmpxx::error>([] { return fu_chain<depth>(); });
  return r.has_value() ? 0 : r.error().code;
}

struct ex {
  int code;
};

template <int D>
int th_chain() {
  guard g{D};
  if constexpr (D <= 0)
    throw ex{42};
  else
    return th_chain<D - 1>() + g.id;
}

int th_escape() {
  try {
    return th_chain<depth>();
  } catch (const ex& e) {
    return e.code;
  }
}

struct sample {
  double arm_ns = 0;
  double throw_ns = 0;
};

// Both paths are timed in one loop so every thread meets the same contention, and the
// injected defect serializes the arm alone, which is what a lock in the escape path
// would do to a real program.
sample measure(int threads, int iters) {
  std::vector<long long> arm(static_cast<std::size_t>(threads), 0);
  std::vector<long long> thr(static_cast<std::size_t>(threads), 0);
  std::vector<std::thread> pool;
  pool.reserve(static_cast<std::size_t>(threads));
  for (int t = 0; t < threads; ++t) {
    pool.emplace_back([&, t] {
      using clock = std::chrono::steady_clock;
      for (int i = 0; i < 200; ++i) t_sink = t_sink + fu_escape() + th_escape();
      long long a = 0, b = 0;
      for (int i = 0; i < iters; ++i) {
        auto t0 = clock::now();
        if (g_serialize) {
          std::lock_guard<std::mutex> hold(g_lock);
          t_sink = t_sink + fu_escape();
        } else {
          t_sink = t_sink + fu_escape();
        }
        auto t1 = clock::now();
        t_sink = t_sink + th_escape();
        auto t2 = clock::now();
        a += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        b += std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
      }
      arm[static_cast<std::size_t>(t)] = a;
      thr[static_cast<std::size_t>(t)] = b;
    });
  }
  for (auto& th : pool) th.join();
  long long a = 0, b = 0;
  for (int t = 0; t < threads; ++t) {
    a += arm[static_cast<std::size_t>(t)];
    b += thr[static_cast<std::size_t>(t)];
  }
  const double ops = static_cast<double>(threads) * iters;
  return {static_cast<double>(a) / ops, static_cast<double>(b) / ops};
}

}  // namespace uwsc

}  // namespace

int probe_unwind_scale(Fmt fmt, const std::vector<std::string>& args) {
  int max_threads = 8;
  int iters = 4000;
  // How much worse than a C++ throw the arm's per-escape latency may inflate as threads
  // are added. Both walk the same unwinder, so the honest expectation is that they
  // inflate together; the margin covers ordinary measurement noise on a shared machine.
  double bound = 2.0;
  for (std::size_t i = 0; i < args.size(); ++i) {
    auto next = [&]() { return (i + 1 < args.size()) ? args[++i] : std::string(); };
    if (args[i] == "--max-threads") max_threads = std::atoi(next().c_str());
    else if (args[i] == "--iters") iters = std::atoi(next().c_str());
    else if (args[i] == "--bound-factor") bound = std::atof(next().c_str());
    else if (args[i] == "--inject-serialization") uwsc::g_serialize = true;
  }
  const unsigned cores = std::thread::hardware_concurrency();
  if (cores && max_threads > static_cast<int>(cores)) max_threads = static_cast<int>(cores);
  if (max_threads < 2) max_threads = 2;

  Report r(fmt, "unwind-scale");
  if (!unwind::available()) {
    r.note("the unwind arm has no backend on this target; nothing to drive");
    return r.finish();
  }
  r.num("cores", static_cast<long long>(cores));
  r.num("iters_per_thread", iters);

  uwsc::sample base{};
  double arm_inflation = 1.0, throw_inflation = 1.0;
  for (int t = 1; t <= max_threads; t *= 2) {
    const uwsc::sample s = uwsc::measure(t, iters);
    if (t == 1) base = s;
    arm_inflation = s.arm_ns / (base.arm_ns > 0 ? base.arm_ns : 1.0);
    throw_inflation = s.throw_ns / (base.throw_ns > 0 ? base.throw_ns : 1.0);
    const std::string tag = "threads_" + std::to_string(t);
    r.num(tag + ".escape_ns", static_cast<long long>(s.arm_ns));
    r.num(tag + ".throw_ns", static_cast<long long>(s.throw_ns));
    r.num(tag + ".escape_inflation_x100", static_cast<long long>(arm_inflation * 100));
    r.num(tag + ".throw_inflation_x100", static_cast<long long>(throw_inflation * 100));
  }
  const double ratio = arm_inflation / (throw_inflation > 0 ? throw_inflation : 1.0);
  r.num("relative_inflation_x100", static_cast<long long>(ratio * 100));
  r.num("bound_x100", static_cast<long long>(bound * 100));
  r.boolean("scales_with_the_platform", ratio <= bound);
  r.note(uwsc::g_serialize
             ? "serialization injected into the escape path: its latency is expected to "
               "inflate with threads while the throw's does not"
             : "the escape and a C++ throw walk the same unwinder, so their latency "
               "inflates together as threads are added");
  if (ratio > bound)
    r.fail("the escape's latency inflated with threads beyond the committed multiple of "
           "a C++ throw's");
  return r.finish();
}

}  // namespace jv
