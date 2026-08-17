// SPDX-License-Identifier: MIT
// The shared benchmark kernel specification. Every error-handling mechanism the
// suite compares (jmpxx, a hand-written branch, std::expected, Boost.Outcome,
// Boost.LEAF, tl::expected, a threaded error_code, and language exceptions)
// implements the SAME kernel: a fixed-depth chain of frames where each frame does
// one arithmetic step and either forwards a value or produces a failure, ending at
// one landing point. Only the error-handling construct differs between kernels, so
// a measured difference is the mechanism's cost and not a difference in work.
//
// Two fairness controls live here. JXB_NOINLINE_FRAMES forces every frame to be a
// real stack frame, so the propagation actually crosses frame boundaries; without
// it the optimizer collapses an entire chain into a few branchless instructions and
// every mechanism measures as free. The codegen golden checks that optimized case; it
// says nothing about per-hop mechanism cost. The depth and the per-frame work are
// fixed constants every kernel reads, so no kernel can quietly do less work than
// another.
#ifndef JMPXX_BENCH_SPEC_HPP
#define JMPXX_BENCH_SPEC_HPP

// The chain runs jxb_depth wrapper frames above one leaf, so a kernel touches
// jxb_depth + 1 real frames between the entry and the failure site. Eight matches
// the depth the codegen goldens and the harness destructor probe use, so the suite
// measures the same chain those gates pin. The deep depth is the second cell of the
// depth sweep, four times as far, which shows whether a mechanism's per-frame cost
// scales linearly and how far the sad-path gap widens with depth.
inline constexpr int jxb_depth = 8;
inline constexpr int jxb_depth_deep = 32;

// The leaf fails with this code and domain when its input is negative. A kernel's
// landing returns the value on success and the negated code on failure, so the
// benchmark harness can feed an input pattern that fails at a chosen rate and still
// sink a single int per call.
inline constexpr int jxb_fail_code = 42;
inline constexpr int jxb_fail_domain = 7;

// Per-frame work: each wrapper adds one to the value it forwards. Kept to a single
// cheap operation so the payload never dominates the propagation construct the
// benchmark is there to measure.
inline constexpr int jxb_frame_step = 1;

// The frame attribute. When JXB_NOINLINE_FRAMES is defined the chain frames are
// kept as distinct stack frames; otherwise the optimizer is free to inline them,
// which is the configuration the size and compile-cost fixtures use because it is
// what ships. Asked for through the compiler's own spelling; the benchmark kernels
// are dev-only translation units, not public headers, so a direct attribute here
// does not cross the platform fence the library headers hold.
#if defined(JXB_NOINLINE_FRAMES)
#if defined(__GNUC__) || defined(__clang__)
#define JXB_FRAME_ATTR __attribute__((noinline))
#elif defined(_MSC_VER)
#define JXB_FRAME_ATTR __declspec(noinline)
#else
#define JXB_FRAME_ATTR
#endif
#else
#define JXB_FRAME_ATTR
#endif

#endif  // JMPXX_BENCH_SPEC_HPP
