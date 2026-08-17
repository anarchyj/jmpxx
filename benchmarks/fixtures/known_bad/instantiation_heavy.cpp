// SPDX-License-Identifier: MIT
// A deliberately instantiation-heavy fixture for the compile-cost gate's inverted
// self-test. It instantiates the transport and the propagation machinery over a few
// hundred distinct value types, work the hand-written baseline never does, so its
// translation time is far above the baseline's and the ratio exceeds any sane budget.
// If the gate passed this fixture it would pass a real compile-time blowup, so this
// known-bad input checks the negative path.
#include "../../kernels/spec.hpp"

#include <jmpxx/core.hpp>

using jmpxx::error;
using jmpxx::fail;
using jmpxx::result;

namespace {

// Each N yields a distinct value type, so result<Tag<N>, error> and the chain over it
// are distinct instantiations the frontend must elaborate separately.
template <int N>
struct Tag {
  int v;
  char pad[1 + (N % 7)];
};

template <int N>
result<Tag<N>, error> make(int x) {
  if (x < 0) return fail(error(N));
  return Tag<N>{x, {}};
}

// Returns a result so JMPXX_TRY is well-formed here, which instantiates the propagation
// machinery for each N alongside the transport.
template <int N>
result<Tag<N>, error> use(int x) {
  JMPXX_TRY(t, make<N>(x));
  return Tag<N>{t.v + 1, {}};
}

// Recursively expand to instantiate use<0..limit>, and through them make<N> and
// result<Tag<N>, error> for every N, so the frontend pays for hundreds of transports.
template <int N>
int expand(int x) {
  if constexpr (N < 0) {
    return 0;
  } else {
    result<Tag<N>, error> r = use<N>(x);
    return (r.has_value() ? r.value().v : -1) + expand<N - 1>(x);
  }
}

constexpr int limit = 255;

}  // namespace

extern "C" int inst_heavy_chain(int x) { return expand<limit>(x); }
