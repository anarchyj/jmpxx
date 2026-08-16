// SPDX-License-Identifier: MIT
// The escape chain the link-time-optimization tier drives, split across two translation
// units so the optimizer sees the landing and the ejecting leaf together only at link
// time. Whole-program inlining is where the arm's assumptions are most exposed: the
// cleanup landing pads survive only while the optimizer still models the eject as a call
// that may unwind, and the body's automatic objects stay out of the landing frame only
// while the trampoline between them is not inlined away.
//
// The same source builds against the shipped arm and against the regressed arm in
// known_bad/, which is what gives the tier teeth.
#ifndef JMPXX_TEST_UNWIND_LTO_CHAIN_HPP
#define JMPXX_TEST_UNWIND_LTO_CHAIN_HPP

#if JMPXX_TEST_REGRESSED_ARM
#include "known_bad/regressed_arm.hpp"
namespace arm = jmpxx_known_bad;
#else
#include "jmpxx/unwind.hpp"
namespace arm = jmpxx::unwind;
#endif

#include "jmpxx/core.hpp"

// Counters and the runtime trip point live in the landing translation unit; the frames
// read them through these declarations, so nothing about the chain is constant-folded
// away before link time.
extern int g_ctor;
extern int g_dtor;
extern volatile int g_sink;
extern int g_trip;

struct guard {
  int id;
  explicit guard(int i) : id(i) { ++g_ctor; }
  ~guard() { ++g_dtor; g_sink = g_sink + id; }
};

// Eight distinct frames, each owning a guard, defined in the other translation unit.
int chain_top(int seed);

// A self-recursive chain, the shape that exposed cleanup elision when the recursion was
// inlined into one frame.
int recursive_chain(int depth);

#endif  // JMPXX_TEST_UNWIND_LTO_CHAIN_HPP
