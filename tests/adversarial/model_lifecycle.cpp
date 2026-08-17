// SPDX-License-Identifier: MIT
// Model-based lifecycle tier for the portable surface. It checks the transport,
// propagation, and diagnostic landing discipline as one composed state machine
// rather than as independent operations.
#include "jmpxx/core.hpp"
#include "jmpxx/diagnostics.hpp"
#include "jmpxx/erased.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string_view>

namespace {

[[noreturn]] void die(const char* what) {
  std::fprintf(stderr, "model lifecycle: %s\n", what);
  std::abort();
}

struct throw_controls {
  static inline bool copy_ctor = false;
  static inline bool copy_assign = false;
  static inline bool move_assign = false;

  static void reset() noexcept {
    copy_ctor = false;
    copy_assign = false;
    move_assign = false;
  }
};

struct tracked_value {
  int value = 0;

  explicit tracked_value(int v = 0) noexcept : value(v) {}
  tracked_value(const tracked_value& other) : value(other.value) {
    if (throw_controls::copy_ctor) throw 11;
  }
  tracked_value(tracked_value&& other) noexcept : value(other.value) {}
  tracked_value& operator=(const tracked_value& other) {
    if (throw_controls::copy_assign) throw 12;
    value = other.value;
    return *this;
  }
  tracked_value& operator=(tracked_value&& other) {
    if (throw_controls::move_assign) throw 13;
    value = other.value;
    return *this;
  }
};

struct tracked_error {
  int code = 0;

  explicit tracked_error(int c = 0) noexcept : code(c) {}
  tracked_error(const tracked_error& other) : code(other.code) {
    if (throw_controls::copy_ctor) throw 21;
  }
  tracked_error(tracked_error&& other) noexcept : code(other.code) {}
  tracked_error& operator=(const tracked_error& other) {
    if (throw_controls::copy_assign) throw 22;
    code = other.code;
    return *this;
  }
  tracked_error& operator=(tracked_error&& other) {
    if (throw_controls::move_assign) throw 23;
    code = other.code;
    return *this;
  }
};

struct transport_model {
  bool has_value = true;
  int payload = 0;
};

using modeled_result = jmpxx::result<tracked_value, tracked_error>;

modeled_result make_result(const transport_model& model) {
  if (model.has_value) return modeled_result(jmpxx::in_place, model.payload);
  return jmpxx::fail(tracked_error(model.payload));
}

void compare_transport(const modeled_result& actual,
                       const transport_model& expected) {
  if (actual.has_value() != expected.has_value)
    die("transport discriminant diverged from the model");
  if (expected.has_value) {
    if (actual.value().value != expected.payload)
      die("transport value payload diverged from the model");
  } else if (actual.error().code != expected.payload) {
    die("transport error payload diverged from the model");
  }
}

bool assignment_threw(auto&& operation) {
  try {
    static_cast<decltype(operation)&&>(operation)();
    return false;
  } catch (...) {
    return true;
  }
}

jmpxx::result<int, jmpxx::rich_error> modeled_chain(int depth, int seed,
                                                    bool fail_at_leaf) {
  if (depth == 0) {
    if (fail_at_leaf) return jmpxx::fail(jmpxx::rich_error(seed, 7));
    return seed;
  }
  JMPXX_TRY(v, modeled_chain(depth - 1, seed, fail_at_leaf));
  return v + 1;
}

void compare_propagation_result(const jmpxx::result<int, jmpxx::rich_error>& r,
                                int depth, int seed, bool fail_at_leaf) {
  if (!fail_at_leaf) {
    if (!r.has_value() || r.value() != seed + depth)
      die("propagation success value diverged from the model");
    return;
  }

  if (r.has_value()) die("propagation failure reached the model as a value");
  if (r.error().code != seed || r.error().domain != 7)
    die("propagation failure payload diverged from the model");

#if JMPXX_DIAGNOSTICS_ENABLED
  jmpxx::diagnostic::context c = jmpxx::diagnostic::inspect(r.error());
  int expected_hops = depth > jmpxx::diagnostic::max_chain
                          ? jmpxx::diagnostic::max_chain
                          : depth;
  bool expected_truncated = depth > jmpxx::diagnostic::max_chain;
  if (!c.available) die("diagnostic record missing inside its landing scope");
  if (c.hop_count != expected_hops)
    die("diagnostic hop count diverged from the model");
  if (c.hops_truncated != expected_truncated)
    die("diagnostic truncation flag diverged from the model");
#endif
}

void check_nested_landing_lifetime() {
#if JMPXX_DIAGNOSTICS_ENABLED
  jmpxx::rich_error outer_error;
  {
    jmpxx::landing outer;
    auto outer_result = modeled_chain(3, 101, true);
    if (outer_result.has_value()) die("outer failure unexpectedly succeeded");
    outer_error = outer_result.error();
    if (!jmpxx::diagnostic::inspect(outer_error).available)
      die("outer diagnostic record missing inside the outer landing");

    jmpxx::rich_error inner_error;
    {
      jmpxx::landing inner;
      auto inner_result = modeled_chain(2, 202, true);
      if (inner_result.has_value()) die("inner failure unexpectedly succeeded");
      inner_error = inner_result.error();
      if (!jmpxx::diagnostic::inspect(inner_error).available)
        die("inner diagnostic record missing inside the inner landing");
    }

    if (jmpxx::diagnostic::inspect(inner_error).available)
      die("inner diagnostic record outlived its landing scope");
    if (!jmpxx::diagnostic::inspect(outer_error).available)
      die("outer diagnostic record was truncated by the inner landing");
  }

  if (jmpxx::diagnostic::inspect(outer_error).available)
    die("outer diagnostic record outlived its landing scope");
#endif
}

void check_diagnostic_capacity_model() {
#if JMPXX_DIAGNOSTICS_ENABLED
  jmpxx::landing landing;
  jmpxx::rich_error errors[jmpxx::diagnostic::max_inflight + 1];
  for (int i = 0; i <= jmpxx::diagnostic::max_inflight; ++i)
    errors[i] = jmpxx::rich_error(700 + i, 8);

  if (!jmpxx::diagnostic::inspect(errors[jmpxx::diagnostic::max_inflight - 1]).available)
    die("last in-capacity diagnostic record was dropped");
  if (jmpxx::diagnostic::inspect(errors[jmpxx::diagnostic::max_inflight]).available)
    die("over-capacity diagnostic record was retained");
#endif
}

void check_generic_fold_model() {
  jmpxx::erased_error folded(0x1234, 0x0056);
  int expected =
      static_cast<int>(static_cast<unsigned>(0x1234) ^
                       (static_cast<unsigned>(0x0056) << 16));
  if (folded.value() != expected)
    die("type-erased generic fold diverged from the model");

  jmpxx::erased_error overlapping(0x00100000, 0x0010);
  int expected_overlap =
      static_cast<int>(static_cast<unsigned>(0x00100000) ^
                       (static_cast<unsigned>(0x0010) << 16));
  if (overlapping.value() != expected_overlap)
    die("type-erased generic fold overlap case diverged from the model");
}

void run_transport_model(bool inject_wrong_transition) {
  std::mt19937 rng(0x5081212u);

  for (int sequence = 0; sequence < 512; ++sequence) {
    modeled_result actual(jmpxx::in_place, 0);
    transport_model model{};

    for (int step = 0; step < 96; ++step) {
      throw_controls::reset();
      int payload = static_cast<int>(rng());
      int op = static_cast<int>(rng() % 9);

      switch (op) {
        case 0:
          actual = tracked_value(payload);
          model = {true, payload};
          break;
        case 1:
          actual = jmpxx::fail(tracked_error(payload));
          model = {false, payload};
          break;
        case 2: {
          transport_model other{(rng() & 1u) != 0u,
                                static_cast<int>(rng())};
          modeled_result source = make_result(other);
          bool same_state = model.has_value == other.has_value;
          throw_controls::copy_assign = same_state && ((rng() & 3u) == 0u);
          throw_controls::copy_ctor = !same_state && ((rng() & 3u) == 0u);
          bool threw = assignment_threw([&] { actual = source; });
          if (!threw) model = other;
          break;
        }
        case 3: {
          transport_model other{(rng() & 1u) != 0u,
                                static_cast<int>(rng())};
          modeled_result source = make_result(other);
          bool same_state = model.has_value == other.has_value;
          throw_controls::move_assign = same_state && ((rng() & 3u) == 0u);
          bool threw = assignment_threw([&] {
            actual = static_cast<modeled_result&&>(source);
          });
          if (!threw) model = other;
          break;
        }
        case 4: {
          int fallback = static_cast<int>(rng());
          int got = actual.value_or(tracked_value(fallback)).value;
          int want = model.has_value ? model.payload : fallback;
          if (got != want) die("value_or diverged from the model");
          break;
        }
        case 5: {
          int fallback = static_cast<int>(rng());
          int got = actual.error_or(tracked_error(fallback)).code;
          int want = model.has_value ? fallback : model.payload;
          if (got != want) die("error_or diverged from the model");
          break;
        }
        case 6: {
          bool fail_at_leaf = (rng() & 1u) != 0u;
          int depth = static_cast<int>(rng() % 20);
          int seed = static_cast<int>(rng() & 0x7fffu);
          jmpxx::landing landing;
          auto propagated = modeled_chain(depth, seed, fail_at_leaf);
          compare_propagation_result(propagated, depth, seed, fail_at_leaf);
          if (propagated.has_value()) {
            actual = tracked_value(propagated.value());
            model = {true, seed + depth};
          } else {
            actual = jmpxx::fail(tracked_error(propagated.error().code));
            model = {false, seed};
          }
          break;
        }
        case 7: {
          if (model.has_value != actual.has_value())
            die("state query diverged before narrow access");
          if (model.has_value) {
            if (actual.assume_value().value != model.payload)
              die("assume_value diverged from the model");
          } else if (actual.assume_error().code != model.payload) {
            die("assume_error diverged from the model");
          }
          break;
        }
        default:
          check_generic_fold_model();
          break;
      }

      if (inject_wrong_transition && sequence == 7 && step == 11)
        model.has_value = !model.has_value;
      compare_transport(actual, model);
    }
  }
}

void run_propagation_model(bool inject_wrong_transition) {
  for (int depth = 0; depth <= jmpxx::diagnostic::max_chain + 3; ++depth) {
    for (int seed = 1; seed <= 17; seed += 4) {
      jmpxx::landing landing;
      bool fail_at_leaf = (depth + seed) % 3 != 0;
      auto result = modeled_chain(depth, seed, fail_at_leaf);
      int expected_depth = depth;
      if (inject_wrong_transition && depth == 5 && seed == 9)
        expected_depth = depth + 1;
      compare_propagation_result(result, expected_depth, seed, fail_at_leaf);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  bool inject = argc > 1 &&
                std::string_view(argv[1]) == "--inject-wrong-transition";
  check_nested_landing_lifetime();
  check_diagnostic_capacity_model();
  run_transport_model(inject);
  run_propagation_model(inject);
  std::printf("    cases.asked  %d\n    cases.known  %d\n", 4, 4);
  std::printf("model lifecycle: transport, propagation, and landing matched the model\n");
  return 0;
}
