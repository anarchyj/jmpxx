// SPDX-License-Identifier: MIT
// The std::expected kernel: the depth-8 chain expressed with C++23 std::expected and
// the hand-threaded check-and-return propagation a programmer writes, since the
// standard provides no TRY construct. Built with exceptions off, so the kernel never
// calls expected::value (the one throwing member) and uses has_value, operator*,
// error, and std::unexpected, which the freestanding-expected research confirmed are
// the exception-free surface.
#include "spec.hpp"

#include <version>

#if defined(__cpp_lib_expected)
#include <expected>

namespace {

// The same two-int error jmpxx carries, so std::expected<int, exp_error> and
// result<int, error> hold equivalent payloads and the comparison is of mechanism, not
// of error size.
struct exp_error {
  int code;
  int domain;
};

template <int D>
JXB_FRAME_ATTR std::expected<int, exp_error> exp_frame(int x) {
  if constexpr (D == 0) {
    if (x < 0) return std::unexpected(exp_error{jxb_fail_code, jxb_fail_domain});
    return x * 2;
  } else {
    auto r = exp_frame<D - 1>(x);
    if (!r) return std::unexpected(r.error());  // the manual propagation
    return *r + jxb_frame_step;
  }
}

template <int D>
int run(int x) {
  auto r = exp_frame<D>(x);
  return r.has_value() ? *r : -r.error().code;
}

}  // namespace

extern "C" int expected_chain(int x, int depth) {
  return depth >= jxb_depth_deep ? run<jxb_depth_deep>(x) : run<jxb_depth>(x);
}

#else  // std::expected is unavailable on this standard library

extern "C" int expected_chain(int, int) { return -jxb_fail_code; }

#endif
