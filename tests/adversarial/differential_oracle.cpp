// SPDX-License-Identifier: MIT
// Differential tier for the portable transport and bridge promises. Randomized
// operation sequences compare jmpxx::result against an independent model and, where
// available, std::expected. The bridge checks round-trip through the documented
// standard-library boundaries and keeps documented lossy cases out of the lossless
// oracle.
#include "jmpxx/core.hpp"
#include "jmpxx/interop/error_code.hpp"
#include "jmpxx/interop/expected.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string_view>
#include <system_error>

#if JMPXX_INTEROP_HAS_EXPECTED
#include <expected>
#endif

using namespace jmpxx;

namespace {

[[noreturn]] void die(const char* what) {
  std::fprintf(stderr, "differential oracle: %s\n", what);
  std::abort();
}

struct oracle {
  bool has_value = true;
  int value = 0;
  error err{};
};

void compare(const result<int, error>& r, const oracle& o) {
  if (r.has_value() != o.has_value) die("state diverged");
  if (o.has_value) {
    if (r.value() != o.value) die("value diverged");
  } else if (r.error() != o.err) {
    die("error diverged");
  }
}

int wrapped_sub(int a, int b) noexcept {
  return static_cast<int>(static_cast<unsigned>(a) - static_cast<unsigned>(b));
}

#if JMPXX_INTEROP_HAS_EXPECTED
void compare_expected(const result<int, error>& r,
                      const std::expected<int, error>& e) {
  if (r.has_value() != e.has_value()) die("std::expected state diverged");
  if (r.has_value()) {
    if (r.value() != *e) die("std::expected value diverged");
  } else if (r.error() != e.error()) {
    die("std::expected error diverged");
  }
}
#endif

}  // namespace

int main(int argc, char** argv) {
  bool inject = argc > 1 && std::string_view(argv[1]) == "--inject-divergence";
  std::mt19937 rng(0x5082026u);
  long long sequences = 0;
  long long bridge_checks = 0;

  for (int s = 0; s < 4096; ++s) {
    result<int, error> r = 0;
    oracle o{};
#if JMPXX_INTEROP_HAS_EXPECTED
    std::expected<int, error> e = 0;
#endif
    for (int step = 0; step < 128; ++step) {
      int v = static_cast<int>(rng());
      error err(v, static_cast<int>((rng() >> 8) % 23));
      switch (rng() % 8) {
        case 0:
          r = v;
          o = {true, v, {}};
#if JMPXX_INTEROP_HAS_EXPECTED
          e = v;
#endif
          break;
        case 1:
          r = jmpxx::fail(err);
          o = {false, 0, err};
#if JMPXX_INTEROP_HAS_EXPECTED
          e = std::unexpected(err);
#endif
          break;
        case 2: {
          result<int, error> c = r;
          compare(c, o);
          r = c;
          break;
        }
        case 3: {
          result<int, error> m = static_cast<result<int, error>&&>(r);
          compare(m, o);
          r = m;
          break;
        }
        case 4: {
          auto mapped = r.transform([](int x) { return x ^ 0x55aa; });
          oracle mo = o.has_value ? oracle{true, o.value ^ 0x55aa, {}}
                                  : oracle{false, 0, o.err};
          compare(mapped, mo);
          break;
        }
        case 5: {
          auto recovered = r.or_else([](error x) {
            return result<int, error>(wrapped_sub(x.code, x.domain));
          });
          oracle ro =
              o.has_value ? o : oracle{true, wrapped_sub(o.err.code, o.err.domain), {}};
          compare(recovered, ro);
          break;
        }
        case 6: {
          std::error_code ec = to_error_code(err);
          if (!is_jmpxx(ec) || from_error_code(ec) != err)
            die("std::error_code lossless exposure diverged");
          ++bridge_checks;
          break;
        }
        default:
#if JMPXX_INTEROP_HAS_EXPECTED
          compare_expected(from_expected(to_expected(r)), e);
          ++bridge_checks;
#endif
          break;
      }
      if (inject && s == 13 && step == 7) o.has_value = !o.has_value;
      compare(r, o);
#if JMPXX_INTEROP_HAS_EXPECTED
      compare_expected(r, e);
#endif
    }
    ++sequences;
  }

  // What this run compared, beside what it is built to compare. A driver that
  // quietly stops exercising the bridge still passes every comparison it did make.
  std::printf("    cases.asked  %lld\n    cases.known  %d\n", sequences, 4096);
  std::printf("differential oracle: sequences=%lld bridge_checks=%lld expected=%d\n",
              sequences, bridge_checks, JMPXX_INTEROP_HAS_EXPECTED);
  return 0;
}
