<!-- SPDX-License-Identifier: MIT -->
# Verification surface

A header library cannot be run on its own, so the verification tools compile fixtures, measure the
result, and gate it. Every performance, size, and codegen claim in jmpxx is produced here and fails
the build on a regression, and every claim in [../comparison.md](../comparison.md) is reproducible
with the commands below. Two tools make up the surface: `jmpxx-verify` compiles a fixture and
measures it, and `jmpxx-bench` runs a workload and times it. Both take `--format=json` for one
machine-readable object per probe.

## `jmpxx-verify`

Each subcommand reports named metrics and a pass or fail. The behavioral commands run the library and
report what it did. The codegen and size commands compile a fixture and inspect the output.

- `semantics`, `destructors`, `policies`, `diagnostics`, `interop`, `reflect`, `platform` exercise
  the transport contract, the destructor counts across a deep propagation, the three policies over
  one body, the diagnostic context, the bridges, the reflection-derived error metadata, and the
  build's matrix cell.
- `abi-layout` compiles a descriptor of the frozen public types in the ship configuration, reports
  each type's size, alignment, field offsets, and trait bits, and diffs the layout against a
  committed golden, so an unversioned layout change fails the build. See [abi.md](abi.md).
- `unwind` drives the experimental arm and reports its destructor count and sad-path distribution,
  detailed in [unwind.md](unwind.md). `unwind-matrix` runs each case of the arm's per-runtime
  behavior matrix as its own process, because a case whose declared outcome is a termination kills
  the process that runs it, and reports what the C++ runtime in this build did with a handler or a
  barrier on the escape path. It also reports how many cases it asked against how many it knows,
  and fails on a mismatch, because a probe that quietly asks fewer questions still passes every
  question it asked. `unwind-stress` runs a long randomized campaign of escapes across
  threads and reports destructor balance, payload identity, and leaked landing state.
  `unwind-scale` reports per-escape latency as threads are added, against a C++ throw measured in
  the same loop, so the answer is how the arm scales relative to the platform's own unwinder rather
  than an absolute number that would not travel between machines.
- `codegen` compiles a fixture at `-O2` for a target, slices one function's assembly, and diffs it
  against a committed golden, reporting instruction count and stack-spill presence. `release-diff`
  compiles the same operation under the minimal and the rich policy in a release configuration and
  requires their machine code to match, showing that the rich policy is free in release.
- `size` reports the transport size per policy against a no-overhead budget. `alloc` counts heap
  allocations over the paths declared allocation-free, through a replaced global `operator new`.
- `size-delta` compiles a jmpxx fixture and a hand-written baseline to objects in the ship
  configuration and reports the difference in section bytes, which is the library's size cost.
- `compile-cost` compiles a jmpxx fixture and a baseline and reports the translation cost as the
  template-instantiation count, a deterministic metric, and the wall-clock ratio alongside it.
- `levels` reports each propagation level's cost, listed in [propagation-levels.md](propagation-levels.md).
- `platform` also reports the version the compiled header carries, which the
  documented-value gate holds against the version every packaging channel states.
- `all` runs the behavioral and size commands together.

## `jmpxx-bench`

`jmpxx-bench` drives one error-propagation kernel implemented identically for jmpxx, a hand-written
branch, `std::expected`, a threaded `std::error_code`, language exceptions, Boost.Outcome,
Boost.LEAF, and tl::expected, so a measured difference is the mechanism and not the work. The results
and the comparison are in [../comparison.md](../comparison.md).

- `run` sweeps a failure ratio (0, 1, 50, and 100 percent) and a depth (8 and 32) and reports each
  mechanism's per-call latency by median, 90th, and 99th percentile, with the ratio against the
  hand-written baseline.
- `gate` co-measures jmpxx and the hand-written baseline on the happy path and bounds jmpxx's median
  to a multiple of the baseline's, the perf gate.
- `callgrind`, run under `valgrind --tool=callgrind --collect-atstart=no`, counts the instructions a
  region executes. The count is identical on every machine, so it is the deterministic perf gate.

The measurement controls follow the established practice for tiny operations. The kernel frames are
non-inlined so the propagation crosses real stack frames rather than collapsing to a few
instructions. The inputs are a shuffled mix at the target failure rate, so the branch is taken in an
order the predictor cannot learn, with a fixed seed for reproducibility. Each result passes through
an optimization barrier. Each cell calibrates its iteration count to a target time so the happy path
and the far slower exception sad path are both measured well. The reported statistic is the median
over epochs with the high percentiles, not the mean, because the distribution is right-skewed.

## Gates

A gate is a measurement with a committed bound that fails the build when crossed, and each gate is
checked against a known-bad input so it cannot pass silently.

| gate | command | bound | known-bad input |
|------|---------|-------|-----------------|
| codegen | `codegen` | optimized assembly matches the golden | a wrong golden and a spilling fixture |
| release diff | `release-diff` | rich and minimal policies match in release | a fixture whose diagnostic call survives release |
| size | `size` | transport within the no-overhead budget | a transport over budget |
| size delta | `size-delta` | zero bytes over the hand-written baseline | a bloated fixture |
| no-alloc | `alloc` | zero allocations on the declared paths | an allocating path |
| compile cost | `compile-cost` | instantiation count within budget | an instantiation-heavy fixture |
| perf | `bench gate` and the callgrind script | jmpxx within a multiple of the baseline | a deliberately slowed kernel |
| unwind determinism | `unwind` | sad-path tail within a multiple of a throw's | an injected non-deterministic cleanup |
| unwind runtime matrix | `unwind-matrix` | every case lands or terminates within its safe set, and the fixture lists as many cases as the matrix expects | a case that fabricates a success, and a fixture that lists fewer cases |
| unwind stress | `unwind-stress` | no payload mismatch, cleanup imbalance, or leaked landing state | an injected skipped destructor |
| unwind scaling | `unwind-scale` | escape latency inflates with threads within a multiple of a throw's | serialization injected into the escape path |
| unwind link-time optimization | `unwind.lto` CTest tier | destructors balance under link-time and whole-program optimization | an arm whose escape a caller can prove cannot unwind |
| unwind metadata | `unwind.metadata` CTest tier | the escape path's frames carry cleanup landing pads in the emitted tables | the same chain built against the regressed arm |
| unwind concurrency | `unwind.concurrent` CTest tier | many threads escape independently with no race or wrong landing | a shared rather than per-thread landing stack |
| unwind reentrancy | `unwind.reentrancy` CTest tier | each misdirected escape lands or terminates as declared | the refusing cases made to return success |
| abi layout | `abi-layout` | frozen type layout matches the committed golden | a golden claiming a changed layout |
| documented values | `doc_claim` | every value the harness reports that the prose also states matches, and the covered set is at or above its floor | a document stating a value the harness does not report |
| claim backing | `claim.audit` | every claim on the public prose and comment surface carries one recorded disposition naming what backs it | an unbacked claim added to a public document, and a withdrawn wording put back |
| claim provenance | `claim.provenance` | every measured claim records the toolchain, target, and date it came from | a measured claim stripped of its provenance, and one relabeled to dodge the rule |
| canonical home | `claim.canonical_home` | no claim-bearing statement about a named subject has a second home above the recorded similarity | a duplicate pair the tree once carried, put back |
| gate accounting | `gate.accounting` | every gate has a case count recorded outside it, and every gate needing a quiet machine runs alone | a gate's recorded count dropped |
| codegen identity | `codegen.identity` | the shipped chain and the hand-written chain compile to the same instructions | the bloated fixture, whose code differs |
| foreign headers | `interop.foreign_headers` | the public surface builds and runs behind real upstream header trees from ecosystems with differing collision habits | a public header reaching for a name one of those trees defines |
| unwind body frame | `unwind.body_frame` | the escape destroys every object its body constructed, at every optimization level | none: see the note on unfalsifiable defences below |
| adversarial fuzz | `adversarial.fuzz.*` CTest tiers | portable surface has no crash or sanitizer finding under structured, libFuzzer, and AFL input | injected crash seeds for each fuzz path |
| differential | `adversarial.differential` | transport and bridge behavior match an independent oracle | an injected oracle divergence |
| exception safety | `adversarial.exception_safety` | throwing operations do not leave an observable empty or wrong state | an injected empty-state report |
| hardening | `hardening.mode` | checks fire in their mode and vanish below it | a deliberately wrong absence expectation |
| configuration matrix | `config_matrix` | every declared switch cell builds and runs | a deliberately missing matrix cell |
| memory sanitizer | `msan` | portable surface is clean under an MSan-instrumented libc++ | an uninitialized read fixture |
| static analysis | `static_analysis` | public portable headers are analyzer-clean | a null-dereference fixture |
| mutation testing | `mutation.campaign` | at least 90% of representative source mutants are killed, with untriaged survivors forbidden | a deliberately weakened witness suite |
| adversarial coverage | `adversarial.coverage` | at least 82% region and 78% branch coverage over the tracked portable headers, with named critical regions reached | a deliberately narrowed harness |
| model lifecycle | `model.lifecycle` | transport, propagation, and diagnostic landing behavior match the abstract lifecycle model | an injected wrong transition |

The mutation gate mutates the public headers in a temporary include tree and
compiles the portable witness suite against each mutant. The committed population covers
the discriminant, checked accessors, cross-state assignment, propagation branches,
type-erased policy folds, and diagnostic-store capacity, hop, lookup, and truncation
discipline. The coverage gate uses Clang source-based coverage and gates on regions and
branches over named portable headers rather than averaging line coverage across the tree.
The expected bridge region is required on toolchains that expose `std::expected` and is
reported as absent on toolchains where `JMPXX_INTEROP_HAS_EXPECTED=0`.

## What a gate must report

A gate reporting a pass is not the same as a gate having asked its question. Each gate
prints how many cases it checked and how many it knows of, and the number it must reach
is recorded in the acceptance sweep rather than only inside the gate, so trimming a gate
and its own expectation together still fails. The sweep reports the share of the gate set
carrying that accounting and fails below the recorded floor.

A gate whose result depends on how the suite was invoked declares it. The arm's scaling
and sad-path measurements and the perf gate all compare two things measured in the same
loop, and running them beside the rest of the suite reports the machine's load rather
than the code; they are registered to run alone and the accounting check fails if one is
not.

A gate ends in one of three states. It has teeth, proven by a known-bad input it fails.
It is removed, because what it claimed to check is not checked. Or it is recorded as an
unfalsifiable defence: a defence that is real, whose subject could not be made to fail,
kept with the attempts that were tried. `unwind.body_frame` is the one gate in that
state, and it re-runs its failed attempt every sweep, so a compiler that starts breaking
the defence moves it out of that state rather than leaving it there.

`adversarial.usage_scenarios` is a usage tier rather than a gate. It runs fixed-buffer
embedded-style sensor validation, feed decoding with a risk bound, and a storage/page
boundary under extensive hardening, `-fno-exceptions`, `-fno-rtti`, diagnostics off, and
global heap-allocation traps. The tier exists to make the adversarial apparatus touch
strict consumer code shapes as well as synthetic state-machine drivers.

## Structured output

With `--format=json` each probe prints one object: `probe` names the command, `ok` is the pass or
fail boolean, `metrics` maps each metric name to a number, string, or boolean, and `notes` carries
any messages. Continuous integration and the gate scripts consume this rather than parsing prose.
The schema is stable within a major version.

## Acceptance sweep

`verify/acceptance.py` runs the whole suite over a built tree and reduces it to one release
verdict. It runs every tier and every gate through CTest, pairs each gate with its inverted
self-test (the `.teeth` cases), and reports a gate green only when the gate and its inverted test
both pass. A gate with no passing inverted self-test is reported `unteethed` and fails the
verdict, so the report cannot read green while a gate lacks its negative check. It records
the cell's compiler, architecture, and standard and the headline metrics from
`jmpxx-verify`.

```sh
python3 verify/acceptance.py --build-dir build --format json --out acceptance.json
```

The report object carries `cell`, `summary`, `gates` (each with its status, gate members, and
inverted-test members), `tiers`, `metrics`, and a top-level `verdict`. The schema is stable
within a major version. `--self-test` checks the verdict logic itself: it fails a failed
test and a gate with no inverted self-test, and it runs as a tier in the suite.

## Reproducing the comparison

```sh
cmake -S . -B build -G Ninja && cmake --build build

# Identical happy-path codegen and zero size delta against the hand-written baseline.
./build/verify/jmpxx-verify size-delta \
  --fixture benchmarks/fixtures/ship_jmpxx.cpp \
  --baseline benchmarks/fixtures/ship_handwritten.cpp --max-total-delta 0

# Deterministic instruction counts per mechanism (requires valgrind).
valgrind --tool=callgrind --collect-atstart=no --callgrind-out-file=cg.out \
  ./build/benchmarks/jmpxx-bench callgrind --mech jmpxx --fail 0 --iters 100000
grep '^summary:' cg.out

# The wall-clock distribution sweep across mechanisms, ratios, and depths.
./build/benchmarks/jmpxx-bench run

# The compile-cost tax as a deterministic instantiation count.
./build/verify/jmpxx-verify compile-cost --cxx clang++ \
  --fixture benchmarks/fixtures/ship_jmpxx.cpp \
  --baseline benchmarks/fixtures/ship_handwritten.cpp --max-instantiations 320
```
