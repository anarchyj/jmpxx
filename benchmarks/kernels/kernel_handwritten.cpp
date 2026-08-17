// SPDX-License-Identifier: MIT
// The hand-written baseline: the depth-8 chain a programmer compiling with
// exceptions disabled writes without jmpxx, threading the error back through every
// frame by hand. The zero-overhead claim is measured against this form, so it
// carries the same data jmpxx does in the same layout: a union of the value and the
// error discriminated by a flag, returned by value, with a manual check-and-return at
// every frame. If jmpxx adds any cost over this, the size, codegen, and runtime gates
// expose it; the claim is that it adds none.
#include "spec.hpp"

namespace {

// The same 12-byte shape as result<int, error>: the value and the two-int error
// share storage, and a flag selects which is live. A naive baseline would instead carry
// the value and the error side by side in separate fields and pay extra bytes; this
// tight form removes that confound so the comparison isolates abstraction cost, not a
// data-layout choice.
struct hw_result {
  union {
    int value;
    struct {
      int code;
      int domain;
    } err;
  };
  bool ok;
};

template <int D>
JXB_FRAME_ATTR hw_result hw_frame(int x) {
  if constexpr (D == 0) {
    if (x < 0) return hw_result{.err = {jxb_fail_code, jxb_fail_domain}, .ok = false};
    return hw_result{.value = x * 2, .ok = true};
  } else {
    hw_result r = hw_frame<D - 1>(x);
    if (!r.ok) return r;  // the manual propagation JMPXX_TRY replaces
    return hw_result{.value = r.value + jxb_frame_step, .ok = true};
  }
}

template <int D>
int run(int x) {
  hw_result r = hw_frame<D>(x);
  return r.ok ? r.value : -r.err.code;
}

}  // namespace

extern "C" int handwritten_chain(int x, int depth) {
  return depth >= jxb_depth_deep ? run<jxb_depth_deep>(x) : run<jxb_depth>(x);
}
