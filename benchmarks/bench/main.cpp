// SPDX-License-Identifier: MIT
// jmpxx-bench: the distribution benchmark surface. It drives the one error-propagation
// kernel, implemented identically for jmpxx and every incumbent under ../kernels, across a
// failure-ratio sweep and a depth sweep, and reports each mechanism's per-call latency
// distribution by median and high percentile, beside the delta against the hand-written
// branch jmpxx claims to match. It also carries the perf gate: a relative bound on jmpxx's
// latency against a co-measured hand-written baseline, with an inverted self-test from a
// deliberately slowed kernel, and a callgrind mode that counts instructions deterministically.
//
// Measurement discipline follows the established practice for tiny operations. The kernel
// frames are non-inlined so the propagation crosses real stack frames rather than collapsing
// to a few branchless instructions, the inputs are a shuffled mix at the target failure rate
// so the leaf's branch is taken in an order the predictor cannot learn, every result is sunk
// through an optimization barrier so the work is not deleted, and each cell auto-calibrates
// its inner iteration count to a target epoch time so the happy path and the thousand-times
// slower exception sad path are both measured well. The reported statistic is the median over
// epochs with the 90th and 99th percentiles, not the mean, because the distribution is
// right-skewed and the mean follows the outliers.
#include "kernels.hpp"
#include "spec.hpp"

#include "reporter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#if defined(__has_include)
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#define JXB_HAVE_CALLGRIND 1
#endif
#endif
#ifndef JXB_HAVE_CALLGRIND
#define JXB_HAVE_CALLGRIND 0
#define CALLGRIND_START_INSTRUMENTATION
#define CALLGRIND_ZERO_STATS
#define CALLGRIND_TOGGLE_COLLECT
#define CALLGRIND_DUMP_STATS
#endif

using jv::Fmt;
using jv::Report;

namespace {

using chain_fn = int (*)(int, int);

// A measured mechanism: a stable name and its kernel entry. avail is false when the suite
// was built without the backing third-party library, in which case it is reported as
// unavailable rather than driven, so the suite runs wherever it builds.
struct Mech {
  const char* name;
  chain_fn fn;
  bool avail;
};

std::vector<Mech> all_mechanisms() {
  return {
      {"jmpxx", jmpxx_chain, true},
      {"handwritten", handwritten_chain, true},
      {"std_expected", expected_chain, true},
      {"std_error_code", errorcode_chain, true},
      {"exceptions", exceptions_chain, true},
      {"boost_outcome", outcome_chain, JXB_HAVE_OUTCOME},
      {"boost_leaf", leaf_chain, JXB_HAVE_LEAF},
      {"tl_expected", tlexpected_chain, JXB_HAVE_TLEXPECTED},
  };
}

// Force a value to be materialized so the optimizer cannot delete the work that produced
// it, the standard barrier for a microbenchmark. The memory clobber additionally keeps a
// load from being hoisted out of the timed loop.
template <class T>
inline void do_not_optimize(const T& v) {
  asm volatile("" : : "r,m"(v) : "memory");
}

// Inputs for a target failure fraction: a shuffled mix of one failing value (negative, so
// the leaf fails) and succeeding values, so the branch is taken at the requested rate in an
// order a predictor cannot learn. A fixed seed makes the order reproducible across runs and
// machines, so the benchmark is deterministic in everything but timing.
std::vector<int> make_inputs(double fail_fraction, std::size_t n) {
  std::vector<int> v(n);
  std::size_t fails = static_cast<std::size_t>(fail_fraction * static_cast<double>(n) + 0.5);
  for (std::size_t i = 0; i < n; ++i)
    v[i] = (i < fails) ? -1 : static_cast<int>(1 + (i % 7));
  std::mt19937 rng(0x9e3779b9u);
  std::shuffle(v.begin(), v.end(), rng);
  return v;
}

using clock_t_ = std::chrono::steady_clock;
long long ns_since(clock_t_::time_point t0) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t_::now() - t0).count();
}

// Calibrate the inner iteration count so one epoch runs for about target_ns, which keeps the
// epoch well above the clock resolution for the few-nanosecond happy path and keeps the
// thousand-times slower exception sad path from running too long. Doubles until the time is
// reached or a hard cap is hit.
long long calibrate(chain_fn f, int depth, const std::vector<int>& in, long long target_ns) {
  const std::size_t n = in.size();
  long long inner = 256;
  while (inner < (1LL << 30)) {
    long long acc = 0;
    std::size_t idx = 0;
    auto t0 = clock_t_::now();
    for (long long i = 0; i < inner; ++i) {
      acc += f(in[idx], depth);
      if (++idx == n) idx = 0;
    }
    do_not_optimize(acc);
    if (ns_since(t0) >= target_ns) break;
    inner *= 2;
  }
  return inner;
}

struct Dist {
  double median, p90, p99, min;
  long long inner, epochs;
};

Dist percentiles(std::vector<double>& s, long long inner, long long epochs) {
  std::sort(s.begin(), s.end());
  auto at = [&](double p) { return s[static_cast<std::size_t>(p * static_cast<double>(s.size() - 1))]; };
  return {at(0.5), at(0.9), at(0.99), s.front(), inner, epochs};
}

// Measure one mechanism at one depth over one input mix: warm up, then run `epochs` timed
// epochs of `inner` calls each, and report the per-epoch nanoseconds-per-call distribution.
Dist measure(chain_fn f, int depth, const std::vector<int>& in, int epochs, long long target_ns) {
  const std::size_t n = in.size();
  long long inner = calibrate(f, depth, in, target_ns);
  long long warm = inner;
  volatile long long sink = 0;
  for (long long i = 0; i < warm; ++i) sink += f(in[i % n], depth);
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(epochs));
  for (int e = 0; e < epochs; ++e) {
    std::size_t idx = static_cast<std::size_t>(e) % n;
    long long acc = 0;
    auto t0 = clock_t_::now();
    for (long long i = 0; i < inner; ++i) {
      acc += f(in[idx], depth);
      do_not_optimize(acc);
      if (++idx == n) idx = 0;
    }
    long long dt = ns_since(t0);
    sink += acc;
    samples.push_back(static_cast<double>(dt) / static_cast<double>(inner));
  }
  (void)sink;
  return percentiles(samples, inner, epochs);
}

// Round nanoseconds to picoseconds for a long-long metric that keeps sub-nanosecond
// resolution, which the few-nanosecond happy path needs.
long long ps(double ns_val) { return static_cast<long long>(std::llround(ns_val * 1000.0)); }

const char* fail_label(double frac) {
  if (frac <= 0.0) return "0pct";
  if (frac <= 0.011) return "1pct";
  if (frac <= 0.5) return "50pct";
  return "100pct";
}

// run: the full distribution sweep. For each depth and failure ratio, measure every
// available mechanism and report median, p90, and p99 per call, with the ratio against the
// hand-written baseline, so the comparison shows where each mechanism stands, including the
// cells where jmpxx is not the fastest.
int cmd_run(Fmt fmt, int epochs, long long target_ns, std::size_t pool) {
  const double ratios[] = {0.0, 0.01, 0.5, 1.0};
  const int depths[] = {jxb_depth, jxb_depth_deep};
  std::vector<Mech> mechs = all_mechanisms();
  int rc = 0;
  for (int depth : depths) {
    for (double frac : ratios) {
      std::vector<int> in = make_inputs(frac, pool);
      Report r(fmt, std::string("bench.depth") + std::to_string(depth) + "." + fail_label(frac));
      r.num("depth", depth);
      r.num("fail_pct", static_cast<long long>(frac * 100.0 + 0.5));
      r.num("epochs", epochs);
      // Measure every available mechanism, then report each one with its distribution and its
      // ratio against the hand-written baseline, so a reader sees both the absolute latency and
      // how each mechanism stands against the branch jmpxx claims to match.
      std::vector<std::pair<const char*, Dist>> taken;
      double base_median = 0.0;
      for (const Mech& m : mechs) {
        if (!m.avail) {
          r.boolean(std::string(m.name) + ".available", false);
          continue;
        }
        Dist d = measure(m.fn, depth, in, epochs, target_ns);
        if (std::strcmp(m.name, "handwritten") == 0) base_median = d.median;
        taken.emplace_back(m.name, d);
      }
      for (const auto& [name, d] : taken) {
        r.num(std::string(name) + ".median_ps", ps(d.median));
        r.num(std::string(name) + ".p90_ps", ps(d.p90));
        r.num(std::string(name) + ".p99_ps", ps(d.p99));
        if (base_median > 0.0)
          r.num(std::string(name) + ".vs_handwritten_x100",
                static_cast<long long>(d.median / base_median * 100.0));
      }
      rc |= r.finish();
    }
  }
  return rc;
}

// Co-measure two mechanisms interleaved at the epoch level so a transient stall lands on
// both and cancels in their ratio, the relative-bound discipline used for timing gates
// that must not flake on a shared runner. Returns the two distributions.
std::pair<Dist, Dist> measure_pair(chain_fn fa, chain_fn fb, int depth,
                                   const std::vector<int>& in, int epochs, long long target_ns) {
  const std::size_t n = in.size();
  long long inner = std::max(calibrate(fa, depth, in, target_ns),
                             calibrate(fb, depth, in, target_ns));
  volatile long long sink = 0;
  for (long long i = 0; i < inner; ++i) sink += fa(in[i % n], depth) + fb(in[i % n], depth);
  std::vector<double> sa, sb;
  sa.reserve(static_cast<std::size_t>(epochs));
  sb.reserve(static_cast<std::size_t>(epochs));
  for (int e = 0; e < epochs; ++e) {
    std::size_t idx = static_cast<std::size_t>(e) % n;
    long long aa = 0, bb = 0;
    auto t0 = clock_t_::now();
    for (long long i = 0; i < inner; ++i) { aa += fa(in[idx], depth); do_not_optimize(aa); if (++idx == n) idx = 0; }
    long long da = ns_since(t0);
    auto t1 = clock_t_::now();
    for (long long i = 0; i < inner; ++i) { bb += fb(in[idx], depth); do_not_optimize(bb); if (++idx == n) idx = 0; }
    long long db = ns_since(t1);
    sink += aa + bb;
    sa.push_back(static_cast<double>(da) / static_cast<double>(inner));
    sb.push_back(static_cast<double>(db) / static_cast<double>(inner));
  }
  (void)sink;
  return {percentiles(sa, inner, epochs), percentiles(sb, inner, epochs)};
}

// gate: the perf gate. jmpxx and the hand-written baseline are co-measured on the happy path,
// where zero overhead is the claim, and jmpxx's median is bounded to a multiple of the
// baseline's. The bound is generous because the two should be within noise of each other; the
// gate's job is to catch a real regression, not to police single-digit-percent jitter. The
// --slow mode swaps in the deliberately slowed kernel, whose median is far above the bound,
// so the gate's own negative path must fail.
int cmd_gate(Fmt fmt, int epochs, long long target_ns, std::size_t pool, double bound, bool slow) {
  Report r(fmt, "bench.gate");
  // Two kernels are co-measured on the happy path: the one under test and the
  // hand-written baseline it is bounded against.
  r.num("cases.asked", 2);
  r.num("cases.known", 2);
  std::vector<int> in = make_inputs(0.0, pool);  // happy path
  chain_fn jx = slow ? jmpxx_slow_chain : jmpxx_chain;
  auto [a, b] = measure_pair(jx, handwritten_chain, jxb_depth, in, epochs, target_ns);
  double ratio = a.median / (b.median > 0.0 ? b.median : 1.0);
  r.str("measured", slow ? "jmpxx_slow" : "jmpxx");
  r.num("jmpxx.median_ps", ps(a.median));
  r.num("handwritten.median_ps", ps(b.median));
  r.num("ratio_x100", static_cast<long long>(ratio * 100.0));
  r.num("bound_x100", static_cast<long long>(bound * 100.0));
  r.boolean("within_bound", ratio <= bound);
  r.note(slow ? "slowed kernel injected: the ratio is expected to exceed the bound"
              : "jmpxx is within the committed multiple of the hand-written branch");
  if (ratio > bound)
    r.fail("jmpxx happy-path median is " + std::to_string(ratio) +
           "x the hand-written baseline, above the bound " + std::to_string(bound));
  return r.finish();
}

// callgrind: run one mechanism's kernel a fixed number of times inside a collected region, so
// a callgrind run counts only those instructions. The driver script runs this under
// `valgrind --tool=callgrind --collect-atstart=no` once per mechanism and reads the
// deterministic instruction total from the dump. Outside callgrind it is an ordinary run that
// prints the sink, so the binary stays useful without valgrind.
int cmd_callgrind(const std::string& mech_name, int depth, double frac, long long iters) {
  std::vector<Mech> mechs = all_mechanisms();
  chain_fn f = nullptr;
  if (mech_name == "jmpxx_slow") {
    f = jmpxx_slow_chain;
  } else {
    for (const Mech& m : mechs)
      if (mech_name == m.name && m.avail) f = m.fn;
  }
  if (!f) {
    std::fprintf(stderr, "callgrind: unknown or unavailable mechanism '%s'\n", mech_name.c_str());
    return 2;
  }
  std::vector<int> in = make_inputs(frac, 4096);
  const std::size_t n = in.size();
  long long acc = 0;
  std::size_t idx = 0;
  // Collection is deferred at startup (--collect-atstart=no) and toggled on only around the
  // call loop, so the final dump to --callgrind-out-file holds just this region's instruction
  // count. An explicit DUMP_STATS would instead write the region to a separate numbered file
  // and leave the named output empty, so it is deliberately not called here.
  CALLGRIND_ZERO_STATS;
  CALLGRIND_TOGGLE_COLLECT;
  for (long long i = 0; i < iters; ++i) {
    acc += f(in[idx], depth);
    if (++idx == n) idx = 0;
  }
  CALLGRIND_TOGGLE_COLLECT;
  do_not_optimize(acc);
  std::fprintf(stderr, "callgrind: mech=%s depth=%d fail=%.2f iters=%lld sink=%lld\n",
               mech_name.c_str(), depth, frac, iters, acc);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Fmt fmt = Fmt::human;
  std::string cmd;
  std::vector<std::string> rest;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--format=json") fmt = Fmt::json;
    else if (a == "--format=human") fmt = Fmt::human;
    else if (cmd.empty()) cmd = a;
    else rest.push_back(a);
  }
  auto opt = [&](const std::string& key, const std::string& def) {
    for (std::size_t i = 0; i + 1 < rest.size(); ++i)
      if (rest[i] == key) return rest[i + 1];
    return def;
  };
  bool has = [&](const std::string& key) {
    for (const auto& s : rest) if (s == key) return true;
    return false;
  }("--slow");

  int epochs = std::atoi(opt("--epochs", "60").c_str());
  long long target_ns = std::atoll(opt("--epoch-ns", "300000").c_str());
  std::size_t pool = static_cast<std::size_t>(std::atoll(opt("--pool", "4096").c_str()));

  if (cmd == "run") return cmd_run(fmt, epochs, target_ns, pool);
  if (cmd == "gate")
    return cmd_gate(fmt, epochs, target_ns, pool, std::atof(opt("--bound", "1.6").c_str()), has);
  if (cmd == "callgrind")
    return cmd_callgrind(opt("--mech", "jmpxx"), std::atoi(opt("--depth", "8").c_str()),
                         std::atof(opt("--fail", "0").c_str()),
                         std::atoll(opt("--iters", "200000").c_str()));

  std::fprintf(stderr,
               "usage: jmpxx-bench <run|gate|callgrind> [--format=json]\n"
               "  run   [--epochs N] [--epoch-ns NS] [--pool N]\n"
               "  gate  [--bound F] [--slow] [--epochs N]\n"
               "  callgrind --mech <name> [--depth D] [--fail F] [--iters N]\n");
  return 2;
}
