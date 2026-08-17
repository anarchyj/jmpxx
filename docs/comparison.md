<!-- SPDX-License-Identifier: MIT -->
# How jmpxx compares

Compare jmpxx against the error-handling mechanisms a C++ engineer would otherwise reach for,
including the cases where it does not win. Every number here is produced by the verification
surface and is reproducible with the commands at the end. The two central claims are gated in
continuous integration and fail the build on a regression. The incumbent comparison is measured
data, labeled with the configuration it was taken on.

The mechanisms compared are a hand-written branch on a status flag (the code jmpxx replaces),
`std::expected` with hand-threaded checks, a `std::error_code` threaded by hand, language
exceptions, [Boost.Outcome](https://github.com/boostorg/outcome),
[Boost.LEAF](https://github.com/boostorg/leaf), and
[tl::expected](https://github.com/TartanLlama/expected). Each implements one kernel: a chain of
frames where each frame forwards a value or produces a failure, ending at one landing. The kernel
is identical across mechanisms, so a measured difference is the mechanism's cost and not a
difference in work.

## The two claims that are gated

**The happy path is a hand-written branch.** Compiled at `-O2`, an eight-frame `JMPXX_TRY` chain
that returns a value generates the same machine code as the same chain written by hand with a
status flag. The two are identical to the instruction. The committed codegen golden holds that
machine code and fails the build if it drifts, and the binary-size gate reports a delta of zero
bytes against the hand-written baseline. Under callgrind, which counts instructions from a
simulated CPU and so gives the same answer on every machine, the jmpxx chain executes 136
instructions per call and the hand-written chain executes 137.

**The sad path is bounded and deterministic.** When a failure propagates, jmpxx returns it through
the chain as an ordinary value. A language exception instead drives the unwinder, which at the same
depth executes about 29,000 instructions per throw against jmpxx's 128, and in wall-clock time runs
roughly forty to fifty times longer with a far wider tail. That gap is why many projects
disable exceptions, and it is the property jmpxx preserves while keeping destructors correct.

## Where jmpxx does not win

**On a non-inlined happy path, jmpxx is not the fastest mechanism.** When the optimizer cannot
inline the chain, every frame that returns a `result` carries a twelve-byte value-or-error and
checks a flag, work that language exceptions skip entirely on success because an exception carries
nothing until it is thrown. In that configuration exceptions execute 63 instructions per call to
jmpxx's 136, and a `std::error_code` returned through an out-parameter and Boost.LEAF's thin result
also do less per frame. jmpxx ties the other in-band result types, `std::expected` and tl::expected,
at 136 instructions, and comes in just under Boost.Outcome at 140. This cost exists only when the
chain does not inline. When it does inline, the chain folds to a single branchless select and the
per-frame check disappears, which is the inlined case the gates pin. jmpxx's happy-path advantage
is zero overhead over a hand-written branch, not a win over a thrown exception's empty success path.

**Boost.LEAF is the closest rival, and the two metrics disagree about it.** LEAF, like jmpxx, keeps
the error payload out of the return path, and on this machine its wall-clock happy path is faster
than jmpxx's. Under callgrind LEAF executes 275 instructions per happy-path call to jmpxx's 136,
because its machinery to stash an error id is more instructions, and they pipeline well. That is the
limit of an instruction count: callgrind models no pipeline, cache, or branch prediction, so it
measures static work and not time. Both numbers are reported here. jmpxx's transport is half LEAF's
size, twelve bytes against twenty-four, and a jmpxx failure carries its code in band where LEAF's
is retrieved from thread-local storage at the landing. Those are real differences, and raw
happy-path speed on this workload is not one of them.

**A `std::error_code` is fast but larger and lossier.** Threaded by hand it has a cheap happy path,
but its value is sixteen bytes to jmpxx's eight, it carries no causal context, and it offers no
compile-time protection against a dropped failure.

**jmpxx costs more to compile than a bare status return.** A translation unit that uses the
`result` transport for one chain instantiates about 170 templates under Clang 19, against nine for
the hand-written baseline, and takes roughly twice as long to translate. The count is the standard
library's as much as this library's, so it moves with the toolchain: an older Clang and Apple's
Clang each reach the same code in fewer instantiations. The deterministic compile-cost gate bounds
the count against a fixed budget rather than against this figure, so what cannot grow unnoticed is
the cost itself, and the cost over a plain integer return is the cost of the type-level guarantees.

## The measured numbers

Instructions per call, counted under callgrind, deterministic across runs and machines, measured
with g++-13 at `-O2` on x86-64 with eight intermediate frames left non-inlined:

| mechanism        | happy path | failure path |
|------------------|-----------:|-------------:|
| hand-written     |        137 |          112 |
| jmpxx            |        136 |          128 |
| std::expected    |        136 |          200 |
| tl::expected     |        136 |          203 |
| Boost.Outcome    |        140 |          149 |
| std::error_code  |        269 |          180 |
| Boost.LEAF       |        275 |          283 |
| language exceptions |     63 |       29,224 |

Wall-clock per call is machine-specific. The distribution and the ratios below were taken on the
development host (WSL2, g++-13, `-O2`) and are reproducible with `jmpxx-bench run`, which reports the
median and the 90th and 99th percentiles for every cell. They show jmpxx tracking the hand-written
branch on the happy path, and the exception sad path at one to two microseconds where every
result-returning mechanism stays in the tens of nanoseconds. At full failure and depth eight, a
thrown exception measured about forty-seven times a jmpxx escape and about two hundred times the
hand-written branch, with a wider high percentile.

The transport sizes, which the size probe reports, are eight bytes for the minimal jmpxx error,
twelve for `result<int, error>`, twelve for `std::expected` and tl::expected with an equivalent
error, twelve for Boost.Outcome, twenty-four for Boost.LEAF's result, sixteen for a bare
`std::error_code`, and twenty-four for `std::expected<int, std::error_code>`.

## Method and reproduction

The numbers come from the benchmark suite, which keeps the comparison fair by non-inlining the
kernel frames, feeding a shuffled input mix the branch predictor cannot learn, sinking each result
through an optimization barrier, and reporting the median over epochs with the high percentiles
rather than the mean. The methodology and the exact commands to reproduce every number on this page
are in [reference/verification.md](reference/verification.md).
