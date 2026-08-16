// SPDX-License-Identifier: MIT
// The frames the link-time-optimization tier escapes through. They are deliberately small
// and free of side effects the optimizer cannot see through, so nothing but the arm's own
// discipline keeps their cleanups alive once the linker inlines the whole chain.
#include "lto_chain.hpp"

namespace {

int leaf(int acc) {
  guard g{0};
  g_sink = g_sink + acc;
  if (acc >= g_trip) arm::eject(jmpxx::error(900 + acc, 1));
  return acc + g.id;
}

int f1(int a) { guard g{1}; g_sink = g_sink + a; return leaf(a + 1) + g.id; }
int f2(int a) { guard g{2}; g_sink = g_sink + a; return f1(a + 1) + g.id; }
int f3(int a) { guard g{3}; g_sink = g_sink + a; return f2(a + 1) + g.id; }
int f4(int a) { guard g{4}; g_sink = g_sink + a; return f3(a + 1) + g.id; }
int f5(int a) { guard g{5}; g_sink = g_sink + a; return f4(a + 1) + g.id; }
int f6(int a) { guard g{6}; g_sink = g_sink + a; return f5(a + 1) + g.id; }
int f7(int a) { guard g{7}; g_sink = g_sink + a; return f6(a + 1) + g.id; }

}  // namespace

int chain_top(int seed) { return f7(seed); }

int recursive_chain(int depth) {
  guard g{depth};
  g_sink = g_sink + depth;
  // Statement form rather than `return eject(...)`, because the regressed arm this
  // fixture also builds against spells its eject as a noreturn void function.
  if (depth <= 0) arm::eject(jmpxx::error(500, 2));
  return recursive_chain(depth - 1) + g.id;
}
