// SPDX-License-Identifier: MIT
// Fixture: the zero-cost propagation level next to its hand-written equivalent.
// jx_try_branch propagates with JMPXX_TRY; jx_hand_branch threads a status flag
// by hand. The committed goldens hold the two forms to the same shape: the jmpxx
// form adds only the branch the hand-written form already has.
#include "jmpxx/core.hpp"

using namespace jmpxx;

static result<int, int> producer(int x) {
  if (x == 0) return fail(9);
  return x + 1;
}
extern "C" result<int, int> jx_try_branch(int x) {
  JMPXX_TRY(v, producer(x));
  return v * 10;
}

struct hand {
  int value;
  bool ok;
};
static hand hand_producer(int x) {
  if (x == 0) return {9, false};
  return {x + 1, true};
}
extern "C" hand jx_hand_branch(int x) {
  hand r = hand_producer(x);
  if (!r.ok) return r;
  return {r.value * 10, true};
}
