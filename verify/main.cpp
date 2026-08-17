// SPDX-License-Identifier: MIT
// jmpxx-verify: the verification harness. It exposes every observable property
// of the core through one command-line surface with a human and a structured
// (JSON) output mode. Runtime probes measure transport size against a
// no-overhead budget, count allocations on the paths declared allocation-free,
// count destructors across a deep propagation, exercise the transport semantics,
// and report the propagation level costs. The codegen probe compiles a fixture
// with a pinned compiler, normalizes the emitted assembly, and compares it to a
// committed golden while reporting instruction count and stack-spill presence.
//
// Usage: jmpxx-verify <command> [--format=json]
//   size | alloc | destructors | semantics | levels | all
//   codegen --fixture <f> --symbol <s> --golden <g> [--arch x86_64|aarch64]
//           [--forbid-spill] [--max-insns N] [--update]
#include "jmpxx/core.hpp"
#include "jmpxx/diagnostics.hpp"
#include "jmpxx/erased.hpp"
#include "jmpxx/reflect.hpp"
#include "jmpxx/interop/adapt.hpp"
#include "jmpxx/interop/error_code.hpp"
#include "jmpxx/interop/exception.hpp"
#include "jmpxx/interop/expected.hpp"
#include "jmpxx/platform.hpp"
#include "reporter.hpp"
#include "support.hpp"
#include "unwind_probes.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace jmpxx;
using jv::Fmt;
using jv::read_file;
using jv::Report;
using jv::trim;

// Allocation counting. The global operator new is replaced so a probe can check that
// a path performs no heap allocation. Counting is off except inside the measured
// region, so the harness's own allocations are not counted.
namespace {
long long g_allocs = 0;
bool g_count = false;
}  // namespace

void* operator new(std::size_t n) {
  if (g_count) ++g_allocs;
  void* p = std::malloc(n ? n : 1);
  if (!p) std::abort();
  return p;
}
void* operator new[](std::size_t n) {
  if (g_count) ++g_allocs;
  void* p = std::malloc(n ? n : 1);
  if (!p) std::abort();
  return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

// An 8-frame propagation chain (leaf + 7 wrappers) shared by several probes.
result<int, error> leaf(int x) {
  if (x < 0) return fail(error(42));
  return x * 2;
}
result<int, error> w1(int x) { JMPXX_TRY(v, leaf(x)); return v + 1; }
result<int, error> w2(int x) { JMPXX_TRY(v, w1(x)); return v + 1; }
result<int, error> w3(int x) { JMPXX_TRY(v, w2(x)); return v + 1; }
result<int, error> w4(int x) { JMPXX_TRY(v, w3(x)); return v + 1; }
result<int, error> w5(int x) { JMPXX_TRY(v, w4(x)); return v + 1; }
result<int, error> w6(int x) { JMPXX_TRY(v, w5(x)); return v + 1; }
result<int, error> chain8(int x) { JMPXX_TRY(v, w6(x)); return v + 1; }

// One source body, parameterized by the error policy and naming no error type. The
// policies and diagnostics probes instantiate it for each policy, and the alloc
// probe runs it for the rich and erased policies, so a single chain keeps the policy
// as the only thing that changes across the representations.
template <class E>
result<int, E> pol_leaf(int x) {
  if (x < 0) return fail(E(42, 7));
  return x * 2;
}
template <class E>
result<int, E> pol_w1(int x) {
  JMPXX_TRY(v, pol_leaf<E>(x));
  return v + 1;
}
template <class E>
result<int, E> pol_chain(int x) {
  JMPXX_TRY(v, pol_w1<E>(x));
  return v + 1;
}

// size: the transport adds nothing beyond the discriminant and natural padding.
// The budget is the size of a hand-written union-plus-flag with the same payload.
template <class V, class E>
struct hand_written {
  union {
    V v;
    E e;
  };
  bool ok;
};

int probe_size(Fmt fmt, const std::vector<std::string>& args) {
  bool inject = false;
  for (const std::string& a : args)
    if (a == "--inject-oversize") inject = true;
  Report r(fmt, "size");
  long long asked = 0;
  auto check = [&](const char* name, std::size_t got, std::size_t budget,
                   bool triv_inputs, bool triv_result) {
    ++asked;
    r.num(std::string(name) + ".sizeof", static_cast<long long>(got));
    r.num(std::string(name) + ".budget", static_cast<long long>(budget));
    if (got > budget)
      r.fail(std::string(name) + ": transport exceeds the no-overhead budget");
    if (triv_inputs && !triv_result)
      r.fail(std::string(name) +
             ": trivially copyable inputs did not yield a trivially copyable "
             "transport");
  };
#define SIZE_CHECK(T, E)                                                     \
  do {                                                                       \
    using R = result<T, E>;                                                  \
    using V = std::conditional_t<std::is_void_v<T>, detail::unit, T>;        \
    check(#T "," #E, sizeof(R), sizeof(hand_written<V, E>),                  \
          std::is_trivially_copyable_v<V> && std::is_trivially_copyable_v<E>,\
          std::is_trivially_copyable_v<R>);                                  \
  } while (0)
  SIZE_CHECK(int, int);
  SIZE_CHECK(int, error);
  SIZE_CHECK(double, error);
  SIZE_CHECK(char, char);
  SIZE_CHECK(void, error);
#undef SIZE_CHECK

  // The gate's inverted self-test. A transport carrying one word beyond the
  // value-or-error union is over the budget the hand-written form sets, so the
  // comparison must reject it. Without this the size gate could pass because every
  // measured type happens to fit rather than because the budget is enforced.
  if (inject) {
    struct widened {
      result<int, error> transport;
      int extra;
    };
    check("int,error.injected_widening", sizeof(widened),
          sizeof(hand_written<int, error>), true, true);
  }
  // Five type pairs are measured; the sixth exists only in the inverted run.
  r.num("cases.asked", asked);
  r.num("cases.known", inject ? 6 : 5);
  if (asked != (inject ? 6 : 5))
    r.fail("the size probe measured " + std::to_string(asked) +
           " type pairs and the gate expects " + (inject ? "6" : "5"));
  return r.finish();
}

// alloc: construction, inspection, extraction, assignment, and propagation over
// trivial payloads perform no heap allocation. The --inject-alloc self-test
// performs one heap allocation inside the measured region, which the hooked
// allocator must observe, so the gate fails. Without it the gate could pass while
// never exercising an allocating path.
int probe_alloc(Fmt fmt, const std::vector<std::string>& args) {
  bool inject = false;
  for (const std::string& a : args)
    if (a == "--inject-alloc") inject = true;
  volatile long long sink = 0;
  g_allocs = 0;
  g_count = true;
  if (inject) {
    // A deliberate allocation on the measured path, the known-bad input.
    volatile int* leak = new int(7);
    sink += *leak;
    delete leak;
  }
  for (int i = 0; i < 1000; ++i) {
    result<int, error> a = i;
    result<int, error> b = fail(error(i));
    if (a) sink += a.value();
    if (!b) sink += b.error().code;
    result<int, error> ok = chain8(i);
    result<int, error> bad = chain8(-i - 1);
    sink += ok.value_or(0);
    sink += bad.has_value() ? 0 : 1;
    a = b;             // cross-state assignment (value -> error)
    b = i;             // cross-state assignment (error -> value)
    sink += a.has_value() ? 0 : b.value();

    // The no-heap promise holds under every policy, including the rich policy with
    // its diagnostic layer live: its out-of-band store is a fixed per-thread arena,
    // so capturing an origin and a causal chain allocates nothing. The landing
    // scope opening and closing is part of the measured region.
    {
      landing root;
      result<int, rich_error> rok = pol_chain<rich_error>(i);
      result<int, rich_error> rbad = pol_chain<rich_error>(-i - 1);
      sink += rok.value_or(0);
      sink += rbad.has_value() ? 0 : rbad.error().code;
    }
    result<int, erased_error> eok = pol_chain<erased_error>(i);
    result<int, erased_error> ebad = pol_chain<erased_error>(-i - 1);
    sink += eok.value_or(0);
    sink += ebad.has_value() ? 0 : ebad.error().value();
  }
  g_count = false;
  Report r(fmt, "alloc");
  r.num("allocations", g_allocs);
  r.num("iterations", 1000);
  // The minimal, rich and type-erased policies are each driven on the measured path.
  r.num("cases.asked", 3);
  r.num("cases.known", 3);
  r.num("diagnostics_enabled", JMPXX_DIAGNOSTICS_ENABLED);
  r.boolean("allocation_free", g_allocs == 0);
  if (g_allocs != 0)
    r.fail("a policy path allocated heap memory");
  (void)sink;
  return r.finish();
}

// destructors: a failure propagated through eight frames runs the destructor of
// every automatic object on the path exactly once. An instrumented type counts
// constructions and destructions; they must balance.
struct Counted {
  static long long ctor;
  static long long dtor;
  int v;
  explicit Counted(int x) : v(x) { ++ctor; }
  Counted(const Counted& o) : v(o.v) { ++ctor; }
  Counted(Counted&& o) noexcept : v(o.v) { ++ctor; }
  Counted& operator=(const Counted&) = default;
  Counted& operator=(Counted&&) = default;
  ~Counted() { ++dtor; }
};
long long Counted::ctor = 0;
long long Counted::dtor = 0;

result<int, error> d_leaf(int x) {
  Counted guard(x);
  if (x < 0) return fail(error(7));
  return guard.v * 2;
}
result<int, error> d1(int x) { Counted g(x); JMPXX_TRY(v, d_leaf(x)); return v + g.v; }
result<int, error> d2(int x) { Counted g(x); JMPXX_TRY(v, d1(x)); return v + g.v; }
result<int, error> d3(int x) { Counted g(x); JMPXX_TRY(v, d2(x)); return v + g.v; }
result<int, error> d4(int x) { Counted g(x); JMPXX_TRY(v, d3(x)); return v + g.v; }
result<int, error> d5(int x) { Counted g(x); JMPXX_TRY(v, d4(x)); return v + g.v; }
result<int, error> d6(int x) { Counted g(x); JMPXX_TRY(v, d5(x)); return v + g.v; }
result<int, error> d_chain(int x) { Counted g(x); JMPXX_TRY(v, d6(x)); return v + g.v; }

int probe_destructors(Fmt fmt) {
  Report r(fmt, "destructors");
  Counted::ctor = 0;
  Counted::dtor = 0;
  result<int, error> bad = d_chain(-1);  // fails at the leaf, propagates up
  long long after_fail_ctor = Counted::ctor;
  long long after_fail_dtor = Counted::dtor;
  Counted::ctor = 0;
  Counted::dtor = 0;
  result<int, error> ok = d_chain(5);  // succeeds through all frames
  r.num("failure.constructed", after_fail_ctor);
  r.num("failure.destructed", after_fail_dtor);
  r.num("success.constructed", Counted::ctor);
  r.num("success.destructed", Counted::dtor);
  r.num("frames", 8);
  r.num("cases.asked", 2);
  r.num("cases.known", 2);
  if (after_fail_ctor != after_fail_dtor)
    r.fail("failure path skipped or double-ran a destructor");
  if (Counted::ctor != Counted::dtor)
    r.fail("success path skipped or double-ran a destructor");
  if (after_fail_ctor < 8)
    r.fail("fewer than eight frames were exercised on the failure path");
  if (bad.has_value() || bad.error().code != 7)
    r.fail("the originating failure was not delivered to the landing boundary");
  if (!ok.has_value())
    r.fail("the success value was not delivered to the landing boundary");
  return r.finish();
}

// semantics: the exactly-one contract and the access surface behave as specified.
int probe_semantics(Fmt fmt) {
  Report r(fmt, "semantics");
  auto expect = [&](bool cond, const char* what) {
    if (!cond) r.fail(what);
  };
  long long checks = 0;
  auto C = [&](bool cond, const char* what) {
    ++checks;
    expect(cond, what);
  };

  result<int, error> v = 7;
  C(v.has_value() && static_cast<bool>(v) && v.value() == 7 && *v == 7,
    "value access");
  result<int, error> e = fail(error(9));
  C(!e.has_value() && !static_cast<bool>(e) && e.error().code == 9,
    "error access");
  C(v.value_or(0) == 7 && e.value_or(-1) == -1, "value_or");

  result<int, error> c = v;
  C(c.has_value() && c.value() == 7, "copy keeps the value");
  result<int, error> m = static_cast<result<int, error>&&>(c);
  C(m.has_value() && m.value() == 7, "move keeps the value");

  v = fail(error(5));
  C(!v.has_value() && v.error().code == 5, "value-to-error assignment");
  v = 11;
  C(v.has_value() && v.value() == 11, "error-to-value assignment");

  result<void, error> ok{};
  C(ok.has_value(), "void success");
  result<void, error> verr = fail(error(2));
  C(!verr.has_value() && verr.error().code == 2, "void failure");

  C(result<int, error>(3) == 3, "equality against a value");
  C(result<int, error>(fail(error(4))) == failure<error>(error(4)),
    "equality against a failure");
  C(result<int, error>(3) == result<int, error>(3), "equality against a result");

  r.num("checks", checks);
  r.num("cases.asked", checks);
  r.num("cases.known", 12);
  if (checks != 12)
    r.fail("the semantics probe ran " + std::to_string(checks) +
           " checks and the gate expects 12");
  return r.finish();
}

// levels: the declared cost and reach of each propagation level. The doc-claim
// tier compares these to the reference documentation.
int probe_levels(Fmt fmt) {
  Report r(fmt, "levels");
  r.str("try.cost", "one conditional branch; no allocation; no frame");
  r.str("try.reach", "propagates one frame per use and composes to any depth");
  r.str("scope.cost", "one call frame unless inlined; no allocation");
  r.str("scope.reach", "one typed landing boundary for a region");
  r.num("cases.asked", 4);
  r.num("cases.known", 4);
  return r.finish();
}

int probe_policies(Fmt fmt) {
  Report r(fmt, "policies");
  auto behaves = [&](const char* name, auto good, auto bad) {
    bool ok = good.has_value() && good.value() == 12 && !bad.has_value();
    r.boolean(std::string(name) + ".identical_behavior", ok);
    if (!ok)
      r.fail(std::string(name) +
             ": a policy changed observable behavior over identical source");
  };
  behaves("minimal", pol_chain<error>(5), pol_chain<error>(-1));
  behaves("rich", pol_chain<rich_error>(5), pol_chain<rich_error>(-1));
  behaves("erased", pol_chain<erased_error>(5), pol_chain<erased_error>(-1));

  // The same propagation construct serves every policy, and every policy keeps the
  // transport trivially copyable and register-sized.
  r.boolean("rich.trivially_copyable", std::is_trivially_copyable_v<rich_error>);
  r.boolean("erased.trivially_copyable",
            std::is_trivially_copyable_v<erased_error>);
  r.boolean("result_rich.trivially_copyable",
            std::is_trivially_copyable_v<result<int, rich_error>>);
  r.boolean("result_erased.trivially_copyable",
            std::is_trivially_copyable_v<result<int, erased_error>>);
  r.num("sizeof.result_minimal",
        static_cast<long long>(sizeof(result<int, error>)));
  r.num("sizeof.result_rich",
        static_cast<long long>(sizeof(result<int, rich_error>)));
  r.num("diagnostics_enabled", JMPXX_DIAGNOSTICS_ENABLED);
  r.num("cases.asked", 3);
  r.num("cases.known", 3);
  if (!std::is_trivially_copyable_v<result<int, rich_error>> ||
      !std::is_trivially_copyable_v<result<int, erased_error>>)
    r.fail("a policy made the transport non-trivially-copyable");
  return r.finish();
}

// An 8-frame chain over the rich policy, so the diagnostics probe reports a chain
// with real depth rather than a token one.
result<int, rich_error> di_leaf(int x) {
  if (x < 0) return fail(rich_error(7, 3));
  return x * 2;
}
result<int, rich_error> di1(int x) { JMPXX_TRY(v, di_leaf(x)); return v + 1; }
result<int, rich_error> di2(int x) { JMPXX_TRY(v, di1(x)); return v + 1; }
result<int, rich_error> di3(int x) { JMPXX_TRY(v, di2(x)); return v + 1; }
result<int, rich_error> di4(int x) { JMPXX_TRY(v, di3(x)); return v + 1; }
result<int, rich_error> di5(int x) { JMPXX_TRY(v, di4(x)); return v + 1; }
result<int, rich_error> di6(int x) { JMPXX_TRY(v, di5(x)); return v + 1; }
result<int, rich_error> di_chain(int x) { JMPXX_TRY(v, di6(x)); return v + 1; }

// diagnostics: in a debug build the rich policy attaches a failure's origin and the
// causal chain it accumulated. The probe drives an 8-frame failure and reports the
// captured origin and per-hop chain; in a release build it reports that the layer
// is absent, which is itself the zero-cost personality.
int probe_diagnostics(Fmt fmt) {
  Report r(fmt, "diagnostics");
  r.num("diagnostics_enabled", JMPXX_DIAGNOSTICS_ENABLED);
#if JMPXX_DIAGNOSTICS_ENABLED
  // The capacity bounds are part of the layer's contract, so the reference states
  // them and the surface reports them rather than the two being written twice.
  r.num("max_chain", diagnostic::max_chain);
  r.num("max_inflight", diagnostic::max_inflight);
  landing root;
  result<int, rich_error> bad = di_chain(-1);  // fails at the leaf, 7 hops up
  if (bad.has_value()) {
    r.fail("the failure did not propagate to the landing boundary");
    return r.finish();
  }
  diagnostic::context c = diagnostic::inspect(bad.error());
  r.boolean("context_available", c.available);
  if (!c.available) {
    r.fail("no diagnostic context was captured for the failure");
    return r.finish();
  }
  r.str("origin.function", c.origin.function_name());
  r.num("origin.line", static_cast<long long>(c.origin.line()));
  r.num("chain.hops", c.hop_count);
  r.boolean("chain.truncated", c.hops_truncated);
  // The leaf plus six wrappers each contribute one hop as the failure climbs to
  // di_chain: seven propagation sites.
  if (c.hop_count != 7)
    r.fail("the causal chain did not record each propagation hop (expected 7, got " +
           std::to_string(c.hop_count) + ")");
  r.boolean("trace_available", platform::trace_available());

  // The success path attaches nothing, and the rich failure decays to the minimal
  // error losslessly in its in-band fields.
  result<int, rich_error> ok = di_chain(5);
  if (!ok.has_value()) r.fail("the success path did not deliver its value");
  error decayed = bad.error().base();
  if (decayed.code != 7 || decayed.domain != 3)
    r.fail("the rich error did not decay to the minimal error losslessly");
#else
  r.note(
      "diagnostic layer compiled out in this build; the rich policy is identical "
      "to the minimal policy");
#endif
  return r.finish();
}

// codegen: compile a fixture with a pinned compiler, slice and normalize the
// target function's assembly, and compare it to a committed golden.
bool write_file(const std::string& p, const std::string& c) {
  std::ofstream f(p);
  f << c;
  return static_cast<bool>(f);
}

struct Normalized {
  std::string text;
  long long instructions = 0;
  bool spilled = false;
  bool found = false;
};

// Keep instruction and label lines of one function; drop directives, comments,
// and blank lines. For a pinned compiler and flags the output is reproducible,
// so local label names are stable and need no renaming. Flag any stack traffic
// (a spill or a frame) so a zero-overhead fixture can require its absence.
Normalized normalize(const std::string& asm_text, const std::string& symbol,
                     const std::string& arch) {
  Normalized n;
  std::istringstream in(asm_text);
  std::string line;
  bool inside = false;
  const bool x86 = (arch == "x86_64");
  const std::string start = symbol + ":";
  while (std::getline(in, line)) {
    std::string t = trim(line);
    if (!inside) {
      if (t == start) {
        inside = true;
        n.found = true;
      }
      continue;
    }
    if (t.rfind(".size", 0) == 0 || t.rfind(".cfi_endproc", 0) == 0) break;
    auto hash = t.find('#');  // GCC uses '#' for asm comments on x86
    if (hash != std::string::npos) t = trim(t.substr(0, hash));
    if (t.empty()) continue;
    if (t[0] == '.' && t.back() != ':') continue;  // assembler directive
    n.text += t;
    n.text += '\n';
    if (t.back() == ':') continue;  // label, not an instruction
    ++n.instructions;
    if (x86) {
      if (t.find("(%rsp)") != std::string::npos ||
          t.find("(%rbp)") != std::string::npos ||
          t.rfind("push", 0) == 0 || t.rfind("pop", 0) == 0)
        n.spilled = true;
    } else {
      if (t.find("[sp") != std::string::npos ||
          t.find("[x29") != std::string::npos || t.rfind("stp", 0) == 0 ||
          t.rfind("ldp", 0) == 0)
        n.spilled = true;
    }
  }
  return n;
}

// Rename compiler-assigned local labels (.L<n>, .LC<n>) to a canonical sequence by
// first appearance. Two functions that differ only in label numbering, for example
// because one is emitted after the other in the same translation unit, then compare
// equal. Label names carry no semantics; the control flow they encode is preserved
// because every occurrence of one name maps to the same canonical token. Apply this
// only when diffing two functions against each other, never against a
// committed golden, so the golden tier keeps comparing exact text.
std::string canon_labels(const std::string& body) {
  auto id = [](char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '$';
  };
  std::map<std::string, std::string> seen;
  std::string out;
  out.reserve(body.size());
  for (std::size_t i = 0, n = body.size(); i < n;) {
    if (body[i] == '.' && i + 2 < n && body[i + 1] == 'L' && id(body[i + 2])) {
      std::size_t j = i + 2;
      while (j < n && id(body[j])) ++j;
      std::string tok = body.substr(i, j - i);
      auto it = seen.find(tok);
      if (it == seen.end())
        it = seen.emplace(tok, ".L#" + std::to_string(seen.size())).first;
      out += it->second;
      i = j;
    } else {
      out += body[i++];
    }
  }
  return out;
}

int probe_codegen(Fmt fmt, const std::vector<std::string>& args) {
  std::string fixture, symbol, golden, arch = "x86_64";
  bool update = false, forbid_spill = false;
  long long max_insns = -1;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto next = [&]() { return (i + 1 < args.size()) ? args[++i] : std::string(); };
    if (a == "--fixture") fixture = next();
    else if (a == "--symbol") symbol = next();
    else if (a == "--golden") golden = next();
    else if (a == "--arch") arch = next();
    else if (a == "--update") update = true;
    else if (a == "--forbid-spill") forbid_spill = true;
    else if (a == "--max-insns") max_insns = std::atoll(next().c_str());
  }
  Report r(fmt, "codegen");
  r.str("fixture", fixture);
  r.str("symbol", symbol);
  r.str("arch", arch);
  r.num("cases.asked", 1);
  r.num("cases.known", 1);
  if (fixture.empty() || symbol.empty() || golden.empty()) {
    r.fail("codegen requires --fixture, --symbol, and --golden");
    return r.finish();
  }

  // Pinned compiler per architecture; overridable for diagnosis via the
  // environment. Goldens are reproducible only against the pinned toolchain.
  std::string cxx;
  if (const char* env = std::getenv("JMPXX_CG_CXX"))
    cxx = env;
  else if (arch == "x86_64")
    cxx = "g++-13";
  else if (arch == "aarch64")
    cxx = "aarch64-linux-gnu-g++";
  else {
    r.fail("unknown --arch (expected x86_64 or aarch64)");
    return r.finish();
  }

  const std::string asm_out = "/tmp/jmpxx_cg.s";
  const std::string err_out = "/tmp/jmpxx_cg.err";
  std::string cmd = cxx +
      " -std=c++20 -O2 -fno-exceptions -fno-rtti"
      " -fno-asynchronous-unwind-tables -fno-ident -S"
      " -I" JMPXX_INCLUDE_DIR " -o " + asm_out + " " + fixture + " 2> " + err_out;
  r.str("compiler", cxx);
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    r.fail("fixture failed to compile: " + trim(read_file(err_out)));
    return r.finish();
  }

  Normalized n = normalize(read_file(asm_out), symbol, arch);
  if (!n.found) {
    r.fail("symbol '" + symbol + "' not found in the emitted assembly");
    return r.finish();
  }
  r.num("instructions", n.instructions);
  r.boolean("spilled", n.spilled);

  // The spill and instruction-count assertions hold regardless of --update, so
  // a known-bad fixture is rejected even while its golden is being written.
  if (forbid_spill && n.spilled)
    r.fail("a stack spill or frame appeared on a path declared zero-overhead");
  if (max_insns >= 0 && n.instructions > max_insns)
    r.fail("instruction count " + std::to_string(n.instructions) +
           " exceeds the budget " + std::to_string(max_insns));

  if (update) {
    if (!write_file(golden, n.text))
      r.fail("could not write golden " + golden);
    else
      r.note("golden updated: " + golden);
    return r.finish();
  }

  std::string want = read_file(golden);
  bool matches = (want == n.text);
  r.boolean("golden_match", matches);
  if (!matches)
    r.fail("optimized codegen diverged from the committed golden");
  return r.finish();
}

// release-diff: the release check for the dual-personality promise, and the
// instruction-level identity check for the zero-overhead promise.
//
// In its one-fixture form two functions perform the same operation, one under the
// minimal policy and one under the rich policy. Compiled in a release configuration
// the diagnostic layer is gone, so the two must generate identical code. With
// --fixture-b the same comparison spans two translation units, which is what the
// zero-overhead claim needs: the jmpxx chain and the hand-written chain are separate
// programs that must nevertheless compile to the same instructions. The gate compares
// label-canonicalized bodies, so two functions that differ only in the compiler's
// local label numbering still compare equal.
//
// It also scans for forbidden strings, because a source location materializes its
// file and function names into read-only data even where the value is unused, a leak
// an instruction diff alone would miss.
int probe_release_diff(Fmt fmt, const std::vector<std::string>& args) {
  std::string fixture, fixture_b, sym_a, sym_b, arch = "x86_64", expect = "equal";
  std::vector<std::string> forbid;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto next = [&]() { return (i + 1 < args.size()) ? args[++i] : std::string(); };
    if (a == "--fixture") fixture = next();
    else if (a == "--fixture-b") fixture_b = next();
    else if (a == "--symbol-a") sym_a = next();
    else if (a == "--symbol-b") sym_b = next();
    else if (a == "--arch") arch = next();
    else if (a == "--expect") expect = next();
    else if (a == "--forbid-string") forbid.push_back(next());
  }
  if (sym_b.empty()) sym_b = sym_a;  // two fixtures commonly share the symbol name
  Report r(fmt, "release-diff");
  r.str("fixture", fixture);
  r.str("fixture_b", fixture_b);
  r.str("symbol_a", sym_a);
  r.str("symbol_b", sym_b);
  r.str("expect", expect);
  // Two functions compared, whether they come from one fixture or two. Reporting the
  // count keeps a gate that quietly compares one thing visible.
  r.num("cases.asked", 2);
  r.num("cases.known", 2);
  if (fixture.empty() || sym_a.empty() || sym_b.empty()) {
    r.fail("release-diff requires --fixture, --symbol-a, and --symbol-b");
    return r.finish();
  }

  std::string cxx;
  if (const char* env = std::getenv("JMPXX_CG_CXX"))
    cxx = env;
  else if (arch == "x86_64")
    cxx = "g++-13";
  else if (arch == "aarch64")
    cxx = "aarch64-linux-gnu-g++";
  else {
    r.fail("unknown --arch (expected x86_64 or aarch64)");
    return r.finish();
  }

  const std::string asm_out = "/tmp/jmpxx_rd.s";
  const std::string asm_out_b = "/tmp/jmpxx_rd_b.s";
  const std::string err_out = "/tmp/jmpxx_rd.err";
  // A release configuration: NDEBUG turns the diagnostic layer off, which is the
  // configuration under which the rich policy must equal the minimal policy, and the
  // ship configuration the zero-overhead claim is made about.
  const std::string flags =
      " -std=c++20 -O2 -DNDEBUG -fno-exceptions -fno-rtti"
      " -fno-asynchronous-unwind-tables -fno-ident -S"
      " -I" JMPXX_INCLUDE_DIR " -I" JMPXX_BENCH_KERNELS_DIR " -o ";
  r.str("compiler", cxx);
  if (std::system((cxx + flags + asm_out + " " + fixture + " 2> " + err_out).c_str()) != 0) {
    r.fail("fixture failed to compile: " + trim(read_file(err_out)));
    return r.finish();
  }
  std::string text = read_file(asm_out);
  std::string text_b = text;
  if (!fixture_b.empty()) {
    if (std::system((cxx + flags + asm_out_b + " " + fixture_b + " 2> " + err_out).c_str()) != 0) {
      r.fail("second fixture failed to compile: " + trim(read_file(err_out)));
      return r.finish();
    }
    text_b = read_file(asm_out_b);
  }

  Normalized a = normalize(text, sym_a, arch);
  Normalized b = normalize(text_b, sym_b, arch);
  if (!a.found || !b.found) {
    r.fail("a target symbol was not found in the emitted assembly");
    return r.finish();
  }
  bool identical = (canon_labels(a.text) == canon_labels(b.text));
  r.num("a.instructions", a.instructions);
  r.num("b.instructions", b.instructions);
  r.boolean("bodies_identical", identical);

  if (expect == "equal" && !identical)
    r.fail(fixture_b.empty()
               ? "release codegen of the rich policy diverged from the minimal policy"
               : "the jmpxx form and the hand-written form no longer compile to the "
                 "same instructions");
  if (expect == "differ" && identical)
    r.fail("expected the two forms to diverge but their release codegen was identical");

  // Forbidden-string scan: a source location, or any debug-only marker, must not
  // reach a release build's read-only data. Scan the emitted string directives of
  // both translation units, because either can carry the leak.
  for (const std::string& mark : forbid) {
    bool leaked = false;
    for (const std::string* emitted : {&text, &text_b}) {
      std::istringstream in(*emitted);
      std::string line;
      while (std::getline(in, line)) {
        std::string t = trim(line);
        if ((t.rfind(".string", 0) == 0 || t.rfind(".ascii", 0) == 0) &&
            t.find(mark) != std::string::npos) {
          leaked = true;
          break;
        }
      }
      if (leaked) break;
    }
    r.boolean("leaked." + mark, leaked);
    if (leaked)
      r.fail("a debug-only string leaked into release read-only data: " + mark);
  }
  return r.finish();
}

// Pick the pinned compiler for an architecture, overridable for diagnosis through
// the environment. Returns an empty string on an unknown architecture. Shared by the
// compile-and-measure probes (codegen pins its own compiler inline; the size and
// compile-cost probes added later use this).
std::string select_cxx(const std::string& arch) {
  if (const char* env = std::getenv("JMPXX_CG_CXX")) return env;
  if (arch == "x86_64") return "g++-13";
  if (arch == "aarch64") return "aarch64-linux-gnu-g++";
  return "";
}

// Run `size` on an object file and return its .text and total (dec) byte counts.
// found is false when the tool output could not be parsed. The Berkeley format prints
// a header line then "text data bss dec hex filename"; the dec field is the section
// total the budget is set against.
struct ObjSize {
  long long text = 0;
  long long total = 0;
  bool found = false;
};
ObjSize measure_obj(const std::string& obj, const std::string& size_tool) {
  ObjSize s;
  const std::string out = "/tmp/jmpxx_size.txt";
  if (std::system((size_tool + " " + obj + " > " + out + " 2>/dev/null").c_str()) != 0)
    return s;
  std::istringstream in(read_file(out));
  std::string line;
  std::getline(in, line);  // header
  if (std::getline(in, line)) {
    std::istringstream nums(line);
    long long text = 0, data = 0, bss = 0, dec = 0;
    if (nums >> text >> data >> bss >> dec) {
      s.text = text;
      s.total = dec;
      s.found = true;
    }
  }
  return s;
}

// size-delta: the binary-size cost the library adds over the hand-written baseline,
// measured deterministically. A fixture that uses jmpxx and a baseline that does the
// same work by hand are each compiled to an object file in the release, no-exceptions,
// no-RTTI strict configuration, and the difference in section bytes is the
// library's size tax. For the zero-overhead kernel the delta is zero. The gate fails
// when a delta exceeds its committed budget. A deliberately bloated fixture exceeds
// the budget and fails as the gate's inverted self-test. The measurement is
// bit-for-bit reproducible for a fixed compiler and flags, so the gate never flakes,
// unlike a wall-clock measurement.
int probe_size_delta(Fmt fmt, const std::vector<std::string>& args) {
  std::string fixture, baseline, arch = "x86_64";
  std::string size_tool = "size";
  long long max_text = 0, max_total = 0;  // budgets in bytes; 0 means require parity
  bool have_text_budget = false, have_total_budget = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto next = [&]() { return (i + 1 < args.size()) ? args[++i] : std::string(); };
    if (a == "--fixture") fixture = next();
    else if (a == "--baseline") baseline = next();
    else if (a == "--arch") arch = next();
    else if (a == "--size-tool") size_tool = next();
    else if (a == "--max-text-delta") { max_text = std::atoll(next().c_str()); have_text_budget = true; }
    else if (a == "--max-total-delta") { max_total = std::atoll(next().c_str()); have_total_budget = true; }
  }
  Report r(fmt, "size-delta");
  r.str("fixture", fixture);
  r.str("baseline", baseline);
  r.str("arch", arch);
  r.num("cases.asked", 2);
  r.num("cases.known", 2);
  if (fixture.empty() || baseline.empty()) {
    r.fail("size-delta requires --fixture and --baseline");
    return r.finish();
  }
  std::string cxx = select_cxx(arch);
  if (cxx.empty()) {
    r.fail("unknown --arch (expected x86_64 or aarch64)");
    return r.finish();
  }
  r.str("compiler", cxx);

  // The strict release configuration: release (NDEBUG drops the diagnostic
  // layer), exceptions and RTTI off, identical for both objects so the delta is the
  // library's contribution and nothing else.
  const std::string flags =
      " -std=c++20 -O2 -DNDEBUG -fno-exceptions -fno-rtti -fno-ident -c"
      " -I" JMPXX_INCLUDE_DIR " -I" JMPXX_BENCH_KERNELS_DIR " ";
  const std::string fobj = "/tmp/jmpxx_sd_fixture.o";
  const std::string bobj = "/tmp/jmpxx_sd_baseline.o";
  const std::string err_out = "/tmp/jmpxx_sd.err";
  if (std::system((cxx + flags + "-o " + fobj + " " + fixture + " 2> " + err_out).c_str()) != 0) {
    r.fail("fixture failed to compile: " + trim(read_file(err_out)));
    return r.finish();
  }
  if (std::system((cxx + flags + "-o " + bobj + " " + baseline + " 2> " + err_out).c_str()) != 0) {
    r.fail("baseline failed to compile: " + trim(read_file(err_out)));
    return r.finish();
  }
  ObjSize f = measure_obj(fobj, size_tool);
  ObjSize b = measure_obj(bobj, size_tool);
  if (!f.found || !b.found) {
    r.fail("could not measure object size (is '" + size_tool + "' available?)");
    return r.finish();
  }
  long long dtext = f.text - b.text;
  long long dtotal = f.total - b.total;
  r.num("fixture.text", f.text);
  r.num("baseline.text", b.text);
  r.num("delta.text", dtext);
  r.num("fixture.total", f.total);
  r.num("baseline.total", b.total);
  r.num("delta.total", dtotal);
  if (have_text_budget) {
    r.num("budget.text_delta", max_text);
    if (dtext > max_text)
      r.fail("text grew " + std::to_string(dtext) + " bytes over the hand-written "
             "baseline, above the budget " + std::to_string(max_text));
  }
  if (have_total_budget) {
    r.num("budget.total_delta", max_total);
    if (dtotal > max_total)
      r.fail("section bytes grew " + std::to_string(dtotal) + " over the hand-written "
             "baseline, above the budget " + std::to_string(max_total));
  }
  return r.finish();
}

// Compile one source once and return the wall-clock translation time in
// milliseconds, or -1 on a compile failure with the error left in err_out. Full
// translation (not syntax-only) so the measurement reflects what a build pays,
// including the backend, at the stated optimization level.
long long compile_once(const std::string& cxx, const std::string& flags,
                       const std::string& src, const std::string& obj,
                       const std::string& err_out) {
  std::string cmd = cxx + flags + " -c -o " + obj + " " + src + " 2> " + err_out;
  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();
  int rc = std::system(cmd.c_str());
  auto t1 = clock::now();
  if (rc != 0) return -1;
  return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

// Count the template instantiations in a translation unit, the deterministic
// compile-cost metric. The source is compiled with Clang's -ftime-trace, which writes
// a JSON profile beside the object, and the InstantiateClass and InstantiateFunction
// events are counted. The count does not vary with machine load, unlike wall-clock
// time, so a gate on it never flakes. Returns -1 if the trace could not be produced,
// for example because the compiler is not a Clang.
long long count_instantiations(const std::string& cxx, const std::string& flags,
                               const std::string& src, const std::string& obj,
                               const std::string& json, const std::string& err_out) {
  std::string cmd = cxx + flags +
                    " -ftime-trace -ftime-trace-granularity=0 -c -o " + obj + " " +
                    src + " 2> " + err_out;
  if (std::system(cmd.c_str()) != 0) return -1;
  std::string trace = read_file(json);
  if (trace.empty()) return -1;
  long long n = 0;
  for (const char* needle : {"\"name\":\"InstantiateClass\"",
                             "\"name\":\"InstantiateFunction\""}) {
    std::size_t pos = 0;
    const std::string key = needle;
    while ((pos = trace.find(key, pos)) != std::string::npos) { ++n; pos += key.size(); }
  }
  return n;
}

// compile-cost: the translation-time tax the library adds over the hand-written
// baseline, gated so a translation-cost regression fails the build. A fixture that
// uses jmpxx and a baseline that does the same work by hand are compiled, and the
// difference is the tax. Templates and the propagation macro cost more to translate
// than a plain status return, so the tax is above zero. The comparison reports it.
//
// The gate is the deterministic template-instantiation count, not wall-clock time,
// because the count does not vary with machine load and so the gate never flakes,
// matching the discipline used for every gate. A fixture's instantiation
// count is bounded by --max-instantiations. An instantiation-heavy
// fixture instantiates the transport over hundreds of distinct types, far past the
// bound, and must fail as the gate's inverted self-test. The wall-clock ratio is
// reported alongside as the visible cost but is not the gate, since it is the
// noisy measure; each fixture is compiled --runs times
// interleaved with the baseline and the minimum is taken, the run least perturbed by
// scheduling noise.
int probe_compile_cost(Fmt fmt, const std::vector<std::string>& args) {
  std::string fixture, baseline, cxx, std_flag = "c++20";
  int runs = 5;
  long long max_inst = -1;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto next = [&]() { return (i + 1 < args.size()) ? args[++i] : std::string(); };
    if (a == "--fixture") fixture = next();
    else if (a == "--baseline") baseline = next();
    else if (a == "--cxx") cxx = next();
    else if (a == "--std") std_flag = next();
    else if (a == "--runs") runs = std::atoi(next().c_str());
    else if (a == "--max-instantiations") max_inst = std::atoll(next().c_str());
  }
  Report r(fmt, "compile-cost");
  r.str("fixture", fixture);
  r.str("baseline", baseline);
  r.num("cases.asked", 2);
  r.num("cases.known", 2);
  if (fixture.empty() || baseline.empty()) {
    r.fail("compile-cost requires --fixture and --baseline");
    return r.finish();
  }
  // Clang provides -ftime-trace, the source of the deterministic count; the default is
  // a Clang and any Clang works for the wall-clock timing too.
  if (cxx.empty()) {
    if (const char* env = std::getenv("JMPXX_CC_CXX")) cxx = env;
    else cxx = "clang++";
  }
  if (runs < 1) runs = 1;
  r.str("compiler", cxx);
  r.num("runs", runs);

  const std::string flags = " -std=" + std_flag + " -O0 -I" JMPXX_INCLUDE_DIR
                            " -I" JMPXX_BENCH_KERNELS_DIR;
  const std::string fobj = "/tmp/jmpxx_cc_fixture.o";
  const std::string bobj = "/tmp/jmpxx_cc_baseline.o";
  const std::string fjson = "/tmp/jmpxx_cc_fixture.json";
  const std::string bjson = "/tmp/jmpxx_cc_baseline.json";
  const std::string err_out = "/tmp/jmpxx_cc.err";

  // The deterministic gate metric: template-instantiation counts.
  long long finst = count_instantiations(cxx, flags, fixture, fobj, fjson, err_out);
  long long binst = count_instantiations(cxx, flags, baseline, bobj, bjson, err_out);
  if (finst >= 0 && binst >= 0) {
    r.num("baseline.instantiations", binst);
    r.num("fixture.instantiations", finst);
    r.num("instantiations.delta", finst - binst);
  } else {
    r.note("instantiation count unavailable (compiler is not a Clang with -ftime-trace)");
  }

  // The informative wall-clock tax, min of interleaved runs, reported but not gated.
  long long fmin = -1, bmin = -1;
  for (int i = 0; i < runs; ++i) {
    long long b = compile_once(cxx, flags, baseline, bobj, err_out);
    if (b < 0) { r.fail("baseline failed to compile: " + trim(read_file(err_out))); return r.finish(); }
    long long f = compile_once(cxx, flags, fixture, fobj, err_out);
    if (f < 0) { r.fail("fixture failed to compile: " + trim(read_file(err_out))); return r.finish(); }
    if (bmin < 0 || b < bmin) bmin = b;
    if (fmin < 0 || f < fmin) fmin = f;
  }
  double ratio = static_cast<double>(fmin) / static_cast<double>(bmin > 0 ? bmin : 1);
  r.num("baseline.min_ms", bmin);
  r.num("fixture.min_ms", fmin);
  r.num("walltime.ratio_x100", static_cast<long long>(ratio * 100));

  if (max_inst >= 0) {
    r.num("budget.instantiations", max_inst);
    if (finst < 0) {
      r.fail("an instantiation budget was set but the count could not be measured "
             "(use a Clang compiler for the compile-cost gate)");
    } else {
      r.boolean("within_budget", finst <= max_inst);
      if (finst > max_inst)
        r.fail("the fixture instantiates " + std::to_string(finst) +
               " templates, above the budget " + std::to_string(max_inst));
    }
  }
  return r.finish();
}

// abi-layout: the observable memory layout of the public types, the ABI promise for
// a header library that ships no object. The probe compiles the layout-descriptor
// fixture in the strict release configuration, with exceptions and RTTI disabled. It
// runs that descriptor to emit the layout of every frozen type and diffs the result
// against a committed golden. A divergence is an unversioned layout change and fails
// the build, the same discipline the codegen golden holds for generated code. Pointed
// at a golden that claims a different layout, it fails, which is the
// unversioned-change case. --update rewrites the golden after a justified, version-bumped
// change. The frozen set and the rationale are in docs/reference/abi.md.
int probe_abi_layout(Fmt fmt, const std::vector<std::string>& args) {
  std::string fixture, golden, arch = "x86_64";
  bool update = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto next = [&]() { return (i + 1 < args.size()) ? args[++i] : std::string(); };
    if (a == "--fixture") fixture = next();
    else if (a == "--golden") golden = next();
    else if (a == "--arch") arch = next();
    else if (a == "--update") update = true;
  }
  Report r(fmt, "abi-layout");
  r.str("fixture", fixture);
  r.str("golden", golden);
  if (fixture.empty() || golden.empty()) {
    r.fail("abi-layout requires --fixture and --golden");
    return r.finish();
  }
  std::string cxx = select_cxx(arch);
  if (cxx.empty()) {
    r.fail("unknown --arch (expected x86_64 or aarch64)");
    return r.finish();
  }
  r.str("compiler", cxx);

  // The ship configuration: release (NDEBUG drops the diagnostic handle so the rich
  // representation is its frozen form), exceptions and RTTI off. The descriptor is
  // compiled to an executable and run, because the layout it reports is what the
  // running program observes.
  const std::string exe = "/tmp/jmpxx_abi_descriptor";
  const std::string out = "/tmp/jmpxx_abi_layout.txt";
  const std::string err_out = "/tmp/jmpxx_abi.err";
  std::string cmd = cxx +
      " -std=c++20 -O2 -DNDEBUG -fno-exceptions -fno-rtti"
      " -I" JMPXX_INCLUDE_DIR " -o " + exe + " " + fixture + " 2> " + err_out;
  if (std::system(cmd.c_str()) != 0) {
    r.fail("descriptor fixture failed to compile: " + trim(read_file(err_out)));
    return r.finish();
  }
  if (std::system((exe + " > " + out + " 2>> " + err_out).c_str()) != 0) {
    r.fail("descriptor fixture failed to run: " + trim(read_file(err_out)));
    return r.finish();
  }
  std::string emitted = read_file(out);

  // Report each frozen type's size as a metric, so the layout is observable through
  // the surface and a continuous-integration run records it without parsing prose.
  std::istringstream in(emitted);
  std::string line;
  while (std::getline(in, line)) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    std::size_t sp = t.find(' ');
    std::string name = (sp == std::string::npos) ? t : t.substr(0, sp);
    std::size_t sz = t.find("sizeof=");
    if (name == "pointer_bits") {
      r.num("pointer_bits", std::atoll(t.c_str() + t.find('=') + 1));
    } else if (sz != std::string::npos) {
      r.num(name + ".sizeof", std::atoll(t.c_str() + sz + 7));
    }
  }

  if (update) {
    if (!write_file(golden, emitted))
      r.fail("could not write golden " + golden);
    else
      r.note("golden updated: " + golden);
    return r.finish();
  }

  std::string want = read_file(golden);
  // Each described type is one case. The golden's own descriptor line count is what
  // the gate knows of, so a descriptor that stops describing a type fails here rather
  // than passing with fewer types checked.
  auto descriptor_lines = [](const std::string& text) {
    long long n = 0;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
      const std::string t = trim(l);
      if (!t.empty() && t[0] != '#') ++n;
    }
    return n;
  };
  r.num("cases.asked", descriptor_lines(emitted));
  r.num("cases.known", descriptor_lines(want));
  if (descriptor_lines(emitted) != descriptor_lines(want))
    r.fail("the descriptor reported " + std::to_string(descriptor_lines(emitted)) +
           " types and the golden holds " + std::to_string(descriptor_lines(want)));
  bool matches = (want == emitted);
  r.boolean("golden_match", matches);
  if (!matches) {
    r.fail("the observed type layout diverged from the committed ABI golden; an "
           "unversioned layout change under a fixed policy");
    r.note("expected:\n" + want);
    r.note("observed:\n" + emitted);
  }
  return r.finish();
}

// interop: exercise each bridge and report its round-trip fidelity, so the bridges
// are observable through the surface rather than only by reading source. The
// std::expected and exception bridges are reported as available or not, because each
// is present only in the configuration that supports it.
int probe_interop(Fmt fmt) {
  Report r(fmt, "interop");

  r.boolean("expected.available", JMPXX_INTEROP_HAS_EXPECTED);
#if JMPXX_INTEROP_HAS_EXPECTED
  {
    result<int, error> rv = 7;
    result<int, error> re = fail(error(9, 3));
    bool ok = from_expected(to_expected(rv)).value() == 7 &&
              from_expected(to_expected(re)).error() == error(9, 3);
    r.boolean("expected.roundtrip_lossless", ok);
    if (!ok) r.fail("std::expected round-trip lost information");
  }
#endif

  {
    error e{5, 2};
    std::error_code ec = to_error_code(e);
    bool expose = from_error_code(ec) == e && is_jmpxx(ec);
    std::error_code foreign = std::make_error_code(std::errc::invalid_argument);
    result<int, std::error_code> carried = fail(foreign);
    bool verbatim = carried.error() == foreign && !is_jmpxx(foreign);
    bool identity = &error_category(2) == &error_category(2) &&
                    &error_category(2) != &error_category(3);
    r.boolean("error_code.expose_recover_lossless", expose);
    r.boolean("error_code.verbatim_carry", verbatim);
    r.boolean("error_code.category_identity_stable", identity);
    if (!expose || !verbatim || !identity)
      r.fail("std::error_code bridge lost fidelity");
  }

  r.boolean("exception.available", JMPXX_INTEROP_HAS_EXCEPTION_BRIDGE);
#if JMPXX_INTEROP_HAS_EXCEPTION_BRIDGE
  {
    result<int, error> bad = fail(error(42, 7));
    result<int, error> back = catch_into_result<error>(
        [&] { return value_or_throw(bad); }, [] { return error(-1); });
    bool ok = !back.has_value() && back.error() == error(42, 7);
    r.boolean("exception.roundtrip_lossless", ok);
    if (!ok) r.fail("exception bridge round-trip lost information");
  }
#endif

  {
    int v = 7;
    int* present = &v;
    int* absent = nullptr;
    bool opt = from_optional<error>(present, [] { return error(1); }).value() == 7 &&
               !from_optional<error>(absent, [] { return error(2); }).has_value();
    bool cond = from_condition<error>(true, [] { return 5; },
                                      [] { return error(3); }).value() == 5;
    r.boolean("adapt.from_optional", opt);
    r.boolean("adapt.from_condition", cond);
    if (!opt || !cond) r.fail("optional-like adapter failed");
  }
  return r.finish();
}

// reflect: report the error metadata the reflection forward layer derives, so the
// capability is observable through the surface rather than only by reading source. The
// harness reports whichever path computed the metadata, the reflection path on a
// reflection-capable build and the hand-written fallback otherwise, and both must
// agree; the probe self-checks the derived values against the known metadata of its
// sample enum, so a regression in either path fails it.
namespace rf {
enum class status { ok = 0, denied = 7, not_found = 2, timeout = 5 };
}  // namespace rf

int probe_reflect(Fmt fmt) {
  using rf::status;
  Report r(fmt, "reflect");
  r.num("reflection_enabled", JMPXX_REFLECTION);
  r.str("type_name", std::string(reflect::type_name<status>()));
  r.num("enum_count", static_cast<long long>(reflect::enum_count<status>()));
  r.str("name.timeout", std::string(reflect::enum_name(status::timeout)));
  r.boolean("name.unmatched_empty",
            reflect::enum_name(static_cast<status>(99)).empty());
  auto cast = reflect::enum_cast<status>("denied");
  r.boolean("cast.denied", cast && *cast == status::denied);
  auto fs = reflect::failures<status>();
  r.num("failures", static_cast<long long>(fs.size()));
  r.num("enum_range.min", reflect::enum_range<status>::min);
  r.num("enum_range.max", reflect::enum_range<status>::max);
  erased_error e = reflect::as_erased(status::not_found);
  r.str("erased.domain", std::string(e.domain_name()));
  r.str("erased.message", std::string(e.message()));

  // The derived metadata must match the sample enum's known shape, value-ordered, so
  // the probe gates the layer's correctness on whichever path produced it.
  bool ok = reflect::enum_name(status::timeout) == "timeout" &&
            reflect::type_name<status>() == "status" &&
            reflect::enum_count<status>() == 4 && fs.size() == 4 &&
            fs[0].value == 0 && fs[0].name == "ok" && fs[3].value == 7 &&
            fs[3].name == "denied" && std::string_view(e.domain_name()) == "status" &&
            std::string_view(e.message()) == "not_found" &&
            (cast && *cast == status::denied);
  if (!ok) r.fail("reflection-derived metadata did not match the sample enum");
  return r.finish();
}

// platform: report the build's matrix cell and the capabilities the configuration
// enables, so a continuous-integration run records each cell's status from the same
// surface. The compiler version is supplied by the runner, which knows it; the
// surface reports what the translation unit can determine on its own.
int probe_platform(Fmt fmt) {
  Report r(fmt, "platform");
  r.str("compiler", platform::compiler_name());
  r.str("os", platform::os_name());
  r.str("arch", platform::arch_name());
  r.boolean("hosted", platform::is_hosted());
  r.num("cpp_standard", static_cast<long long>(JMPXX_CPLUSPLUS));
  r.boolean("diagnostics_enabled", JMPXX_DIAGNOSTICS_ENABLED);
  r.boolean("exceptions_enabled", JMPXX_HAS_EXCEPTIONS);
  r.boolean("expected_bridge", JMPXX_INTEROP_HAS_EXPECTED);
  r.boolean("exception_bridge", JMPXX_INTEROP_HAS_EXCEPTION_BRIDGE);
  r.str("version", JMPXX_VERSION_STRING);
  r.num("version_number", JMPXX_VERSION);
  return r.finish();
}

}  // namespace

int main(int argc, char** argv) {
  Fmt fmt = Fmt::human;
  std::string cmd;
  std::vector<std::string> rest;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--format=json")
      fmt = Fmt::json;
    else if (a == "--format=human")
      fmt = Fmt::human;
    else if (cmd.empty())
      cmd = a;
    else
      rest.push_back(a);
  }

  if (cmd == "size") return probe_size(fmt, rest);
  if (cmd == "alloc") return probe_alloc(fmt, rest);
  if (cmd == "destructors") return probe_destructors(fmt);
  if (cmd == "semantics") return probe_semantics(fmt);
  if (cmd == "levels") return probe_levels(fmt);
  if (cmd == "policies") return probe_policies(fmt);
  if (cmd == "diagnostics") return probe_diagnostics(fmt);
  if (cmd == "interop") return probe_interop(fmt);
  if (cmd == "reflect") return probe_reflect(fmt);
  if (cmd == "platform") return probe_platform(fmt);
  if (cmd == "unwind") return jv::probe_unwind(fmt, rest);
  if (cmd == "unwind-matrix") return jv::probe_unwind_matrix(fmt, rest);
  if (cmd == "unwind-stress") return jv::probe_unwind_stress(fmt, rest);
  if (cmd == "unwind-scale") return jv::probe_unwind_scale(fmt, rest);
  if (cmd == "codegen") return probe_codegen(fmt, rest);
  if (cmd == "release-diff") return probe_release_diff(fmt, rest);
  if (cmd == "size-delta") return probe_size_delta(fmt, rest);
  if (cmd == "compile-cost") return probe_compile_cost(fmt, rest);
  if (cmd == "abi-layout") return probe_abi_layout(fmt, rest);
  if (cmd == "all") {
    int rc = 0;
    rc |= probe_size(fmt, {});
    rc |= probe_alloc(fmt, {});
    rc |= probe_destructors(fmt);
    rc |= probe_semantics(fmt);
    rc |= probe_levels(fmt);
    rc |= probe_policies(fmt);
    rc |= probe_diagnostics(fmt);
    rc |= probe_interop(fmt);
    rc |= probe_reflect(fmt);
    rc |= probe_platform(fmt);
    rc |= jv::probe_unwind(fmt, {});
    return rc;
  }
  std::fprintf(stderr,
               "usage: jmpxx-verify <size|alloc|destructors|semantics|levels|"
               "policies|diagnostics|interop|reflect|platform|unwind|all|codegen|"
               "release-diff|size-delta|compile-cost|abi-layout|unwind-matrix|"
               "unwind-stress|unwind-scale> [--format=json]\n");
  return 2;
}
