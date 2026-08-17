<!-- SPDX-License-Identifier: MIT -->
# How the unwind arm works, and what is measured

This is the design note for the experimental non-local escape: what it does mechanically,
what holds it together, and every measurement behind the guarantees. The caller-facing
contract is in [reference/unwind.md](../reference/unwind.md); this page is the reasoning
and the evidence under it. Read it before putting the arm on a path you care about, and
before changing it.

Every measurement below names the toolchain, the target, and the date it was taken on. A
figure without those is not evidence, and a figure taken once on one machine is worth
what that says and no more.

## What the mechanism is

`escape_scope<E>(body)` plants a landing. `eject(e)` returns to the innermost landing on
the current thread from any depth, and the platform unwinder destroys every automatic
object in between. The frames between the two carry no propagation construct in their
source, which is the whole point: a recursive descent parser or a deep validation chain
reads as if failures could not happen, and still runs its destructors.

The escape is not one mechanism. It is the platform's own, per ABI:

- **Itanium and ARM exception-handling ABIs**, on GCC and Clang: `_Unwind_ForcedUnwind`.
  The landing frame owns a carrier object. The stop function the runtime calls for each
  frame compares that frame's canonical frame address against the carrier's address,
  returning "keep going" while the frame is inside the region to be destroyed, so the
  runtime runs that frame's cleanups; when the walk reaches the landing frame itself, the
  stop function `longjmp`s into the landing.
- **WebAssembly**: the virtual machine has no forced-unwind primitive, so the escape is a
  throw of a private type the machine unwinds to the landing's catch. The intermediate
  destructors run as the compiler's own catch-and-rethrow clauses.
- **Native MSVC**: an unwinding `longjmp` under the C++ exception model, which performs a
  termination unwind and runs the destructors of the frames it passes.

Identifying the landing by the carrier's address rather than by a captured frame depth is
what makes one implementation correct across those ABIs and across optimization levels. It
depends only on the frame address the runtime reports *during* an unwind, which every
supported unwinder implements, and not on reading a frame address outside one, which the
ARM unwinder reports as zero.

## The four properties that hold it together

Each of these has been broken at least once during development, and each break skipped
destructors silently rather than failing loudly. That is why they are written down here
rather than left to the code.

### 1. The escape must not be provably non-unwinding

If a caller can prove that a call neither returns nor unwinds, the optimizer deletes that
call site's cleanup landing pads. The forced unwind then has nothing to run and the escape
destroys **nothing**.

The `[[noreturn]]` attribute alone does not reach this on the compilers in the support
matrix: they keep the pads while the call may still unwind. What does reach it is a
no-unwind proof, which is what `noexcept` or a nothrow declaration gives the optimizer.

Measured on GCC 13.3.0 and Clang 16 through 19, x86-64 Linux, 2026-08-16: an escape
declared `nothrow` and `[[noreturn]]` removes every unwinder resume call from the binary,
and an eight-frame escape then destroys nothing from `-O1` upward. The arm therefore keeps
its escape non-`noreturn`, non-inlinable, and ending on a volatile-guarded path that the
optimizer cannot fold away, so the call always looks like one that may unwind.

This is the property a caller can break from outside the library, by wrapping `eject` in a
helper of their own and declaring it `noexcept`. The reference states that obligation.

### 2. The body must not run in the landing frame

The landing plants a jump buffer, so an object constructed in the landing frame after that
point is not destroyed by the resuming `longjmp`. A non-inlinable trampoline keeps the
body's objects in a frame the unwind destroys.

This defence is weaker than the other three and the difference is worth stating. The other
three each have a subject that fails without them. This one does not: a subject was written
that forces the body inline, and on GCC 13.3.0 and Clang 18 at every optimization level the
objects were still destroyed correctly (measured 2026-08-16, x86-64 Linux). The subject was
removed rather than kept, because a known-bad case that cannot fail reads as coverage
without being any. The trampoline rests on the reasoning above and on the destructor counts,
and that is the honest description of its footing.

### 3. The landing state is per thread

Each thread has its own stack of active scopes and its own in-flight state. Nothing is
shared and no lock is taken, so threads escape independently and an escape never reaches a
landing on another thread. The evidence is under load rather than by inspection; see the
concurrency section.

### 4. The unwind object belongs to the scope, not the thread

A cleanup running during one escape may open a further scope and escape inside it, so two
unwinds can be in flight at once. The runtime keeps its own state for an in-flight forced
unwind inside the unwind object, so a second escape sharing one object overwrites the
first's stop function and landing, and the outer unwind then resumes into a frame that no
longer exists. Each landing scope owns its unwind object, which costs no allocation because
the carrier is already the landing frame's automatic object.

That fix was necessary and not sufficient: with separate objects the nested escape completes
on the Itanium and DWARF ABIs and terminates on the ARM exception-handling ABI. The arm
therefore refuses a second escape while one is unwinding, uniformly on every target, rather
than allowing it where it happens to work.

## Per-runtime behaviour, measured

What a handler on the escape path does to the escape is the C++ runtime's decision, not the
arm's, so the contract is stated per runtime. Each row is one case run as its own process,
because a case whose outcome is a termination kills the process that runs it.

| case | libstdc++ | libc++abi | WebAssembly | MinGW SEH |
| --- | --- | --- | --- | --- |
| no handler on the path | lands | lands | lands | no backend |
| typed catch on the path | lands | lands | lands | no backend |
| cooperative catch-all that rethrows | lands | **terminates** | lands | no backend |
| non-cooperative catch-all | terminates | terminates | terminates | no backend |
| forced-unwind idiom | lands | not available | not available | no backend |
| `noexcept` frame on the path | terminates | terminates | terminates | no backend |
| landing inside a `noexcept` function | lands | lands | lands | no backend |
| cleanup throws and catches its own | lands | lands | lands | no backend |
| nested escape from a cleanup | refused | refused | refused | no backend |
| second escape to the unwinding scope | refused | refused | refused | no backend |

Provenance. libstdc++ measured on x86-64 with GCC 13.3.0 on 2026-08-17, and on AArch64,
RISC-V and 32-bit ARM with the GCC 13.3.0 cross compilers under user-mode emulation on
2026-08-17; the four architectures produce identical rows. libc++abi measured against
libc++ 18 with libc++abi and LLVM's libunwind on x86-64 Linux, 2026-08-16. WebAssembly
measured with Emscripten 3.1.6 under Node 18, 2026-08-17. MinGW-w64 GCC 13 measured under
Wine on 2026-08-17, where the arm reports no backend and an unguarded use is a
compile-time refusal.

The one divergence is the cooperative catch-all: libstdc++ re-raises the in-flight foreign
unwind and the escape transits, while libc++abi's rethrow does not resume a foreign
exception and the program terminates. Both outcomes are loud. Neither is a silent wrong
landing, which is the outcome the arm must never produce and which no runtime produced.

**libcxxrt, the C++ runtime on FreeBSD, is the exception to that and the arm cannot make
it loud.** A frame that catches the escape with a catch-all there does not run its own
destructors, whether it rethrows or consumes it, and when it rethrows the escape still
arrives at its landing carrying the correct error. The caller sees a successful landing and
a leak. A frame marked `noexcept` is skipped the same way rather than terminating. The arm
cannot detect either, because the runtime skips those frames before the arm regains
control. On that runtime an escape path must carry neither a catch-all nor an empty
exception specification. Those three cells are recorded in the behaviour matrix as known
unsafe, and the matrix fails if any of them changes in either direction.

## Destructor evidence

An escape from nine frames runs every destructor exactly once, on GCC 13.3.0 and Clang 16
through 19 at every optimization level, on x86-64 natively and on AArch64, RISC-V and
32-bit ARM under user-mode emulation, measured 2026-08-17. The same count holds on a
bare-metal Cortex-M3 under system emulation and in a trusted application in the secure
world. `jmpxx-verify unwind` reports it, and the inverted subject, an arm whose escape a
caller can prove cannot unwind, destroys nothing.

## Optimization and link-time evidence

Destructor balance holds under `-flto` at `-O2` and `-O3`, under `-flto` with static
linking, under `-flto` with section garbage collection, and under thin and whole-program
modes, with the landing and the frames in separate translation units so the optimizer first
sees the whole escape at link time. Measured on x86-64, AArch64 and RISC-V with GCC 13.3.0
and Clang 18, 2026-08-17. The inverted subject, an arm whose escape a caller can prove
cannot unwind, fails every one of those configurations, which is what gives that evidence
its teeth.

**The ARM exception-handling ABI with link-time optimization is not supported, and the
cause is the toolchain rather than the arm.** With `-flto` the compiler emits a direct
unwinder resume in the cleanup landing pad instead of the ABI's own cleanup exit, and a
forced unwind then terminates as the second frame's cleanup finishes. An ordinary throw
survives it. It reproduces at `-O0` and with fat objects, so it is not an optimizer
decision, and it predates the current arm. No preprocessor macro identifies the option, so
the header cannot refuse the configuration, and the tier pins the limitation instead: on
that ABI the ordinary build must work and the link-time-optimized build must fail, so a
toolchain that fixes it is noticed rather than assumed.

The emitted metadata is audited directly rather than inferred from destructor counts. A
decoder for the frame description entries and the call-site tables reports which functions
carry cleanup landing pads, so a frame that lost them is visible in the binary without
running it.

## Concurrency, reentrancy, and long runs

Eight threads running nested and repeated escapes with typed payloads and interleaved
success and failure paths: no data race under the thread sanitizer, no wrong landing, no
cross-thread payload, and every thread's landing stack empty at the end. The inverted build,
whose landing stack is one global instead of one per thread, dies. Measured on x86-64 with
Clang 18, 2026-08-17.

The seven reentrancy cases the reference enumerates, six refusals and the one shape that
lands, were verified identically on x86-64, AArch64 and 32-bit ARM, 2026-08-17. Each
refusal is a termination with its own diagnostic rather than an undefined outcome, and the
inverted build, in which the refusing cases return success instead, fails.

A campaign of a quarter of a million randomized escapes across threads and seeds, with
depths to fifteen frames and nesting to three levels, reported no payload mismatch, no
cleanup imbalance across roughly two million constructions, and no leaked landing state.

Escaping stays as parallel as the platform's own unwinder and no more. Measured against a
C++ throw in the same loop as threads are added, the escape's latency inflates within seven
per cent of the throw's at eight threads, which is the honest answer: both walk the same
unwinder, so the arm neither adds nor removes contention. The inverted run, which
serializes the escape path alone, inflates twenty-four fold and fails the bound. Measured on
a twelve-core x86-64 host, 2026-08-16.

That measurement is also the one gate in the suite whose result depends on how the suite was
invoked: run beside other work it reports the machine's load rather than the arm's scaling.
It declares that it needs a quiet machine and the suite runs it alone.

## What a caller must do

None of these is checkable by the compiler, which is why they are obligations rather than
guarantees:

- Every frame between an escape and its landing is compiled with exception cleanup tables.
  The header refuses the translation unit that opts in without them; it cannot see the
  others.
- No frame on the escape path is `noexcept`. That terminates on every runtime.
- A catch-all on the escape path is not portable. A typed catch transits everywhere.
- A helper that wraps `eject` is not declared in a way that lets a caller prove it cannot
  unwind. See the first property above for what that costs.
- A destructor running as cleanup during an escape does not escape again.
- The error type matches the receiving scope, and is small and trivially copyable.
- The environment establishes thread-local storage on every thread that enters. An
  environment that does not cannot host the arm's per-thread landing state; a secure-world
  runtime that admits a call on a thread with no thread-local block is a real instance of
  this, and it reads a landing pointer that is not one.

## What is not proven

- **The per-runtime table above was produced on one machine.** Continuous integration runs
  the arm's execution tiers on each reachable ABI, but the ten-case table across four
  runtimes is not yet a continuous-integration cell. A support contract that states
  per-runtime behaviour needs those rows reproduced on every change.
- **Two cells are stale.** libcxxrt's behaviour was measured against an earlier build of the
  arm and has not been re-measured against the current one. Native MSVC was last measured on
  2026-06-24 and its build tools are no longer installed on the host that measured it.
- **The cleanup-table obligation is documented and unenforced.** A consumer who compiles one
  intermediate translation unit without cleanup tables gets an escape that skips that frame's
  destructors, and nothing in the library detects it.

## Status

The arm is experimental and opt-in. It is not on any default path, it is exempt from the
layout-stability promise, and it is not recommended for a stable support contract while the
three items above stand. The portable `result` and `JMPXX_TRY` surface is the default for a
reason: it needs no tables, no runtime, and no obligations beyond checking a result.
