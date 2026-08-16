<!-- SPDX-License-Identifier: MIT -->
# Experimental non-local unwind arm

The unwind arm returns a failure from arbitrary call depth to a single landing boundary
while the platform unwinder runs the destructor of every automatic object on the path.
The intermediate frames carry no propagation construct in their source, which the
portable `JMPXX_TRY` cannot offer. It is the closest standard-C++ approach to a non-local
jump that still preserves RAII, and it is experimental and platform-specific. It is opt-in
and never on a default path: a program reaches it only by including `jmpxx/unwind.hpp`.

The arm is not a replacement for the portable surface. The portable `result` and
`JMPXX_TRY` are the exception-free, table-free default and remain the right choice for
strict embedded and low-latency code. The arm is for code that can afford unwind cleanup
tables and wants the source-oblivious middle frames a deep recursion or a deep call chain
would otherwise have to thread by hand.

## Surface (`jmpxx/unwind.hpp`)

`jmpxx::unwind::escape_scope<E>(body)` runs `body` within a landing boundary and returns
a `result<T, E>`, with `T` deduced from `body`'s return type and `E` defaulting to
`jmpxx::error`. If `body` returns normally its value becomes the result's value; if any
frame it transitively calls invokes `eject<E>`, the stack unwinds back to this scope
running every destructor on the path, and the result holds that error. Scopes nest: an
eject lands at the nearest enclosing `escape_scope`. The landing allocates nothing.

`jmpxx::unwind::eject(err)` performs the escape to the innermost `escape_scope` on the
current thread, carrying `err`. The error type must match the receiving scope and must be
small and trivially copyable, which every error representation the library ships
satisfies; richer context travels out of band, as the resolver in the reference
application does by stashing the offending key before it ejects. `eject` does not return
at run time, but it is deliberately not `[[noreturn]]`: it returns a type convertible to
anything, so `return jmpxx::unwind::eject(err);` type-checks in a function of any return
type. A helper that wraps `eject` returns normally, and must not be declared in a way that
lets its caller prove it cannot unwind, which the caveats below state in full.

`jmpxx::unwind::available()` is a `constexpr bool` that is false on a target with no
backend, where `escape_scope` and `eject` refuse instantiation with a stated
precondition. Branch on it with `if constexpr`, or guard the including translation unit
with `JMPXX_UNWIND_AVAILABLE`, to fall back to the portable surface; a runtime `if` does
not prevent the instantiation.

## The unwind-tables precondition

The arm runs destructors only where the compiler has emitted unwind cleanup tables, which
is the effect of compiling with exceptions enabled. A translation unit that includes
`jmpxx/unwind.hpp` with exceptions disabled is refused at compile time rather than
silently producing an arm that skips destructors. The refusal covers only the translation
unit that opts in; every frame on the escape path must likewise be compiled with cleanup
tables, a runtime precondition the header cannot check. A frame compiled `-fno-exceptions`
on the escape path has its destructors skipped, which is the undefined-RAII outcome the
arm exists to avoid, so the path must be built with tables throughout.

## Escaping while an escape is unwinding

An escape that starts while another is already unwinding on the same thread is refused: the
arm terminates with a diagnostic rather than starting it. That covers both shapes, an
`eject` aimed at the scope whose escape is in flight and an `eject` from a fresh
`escape_scope` a destructor opened as it ran as cleanup.

The refusal is uniform rather than permitted where it happens to work. With each landing
scope owning its own unwind object the second escape completes on the Itanium and DWARF
ABIs, but it terminates on the ARM exception-handling ABI and it is a throw during
unwinding on WebAssembly, which the language terminates. One contract that holds
everywhere is worth more than a capability that aborts on some targets.

A destructor running as cleanup may still open an `escape_scope` and use it, as long as
nothing escapes from that scope while the outer escape is in flight.

Four other ways an `eject` can be misdirected are refused the same way, each with its own
diagnostic: no scope on this thread, an error type the active scope does not receive, a
scope that has already returned, and a scope belonging to another thread.

## Threads

Landing scopes are per thread. Each thread has its own stack of active scopes and its own
in-flight state, so threads escape independently and an escape never reaches a landing on
another thread. Nothing is shared between them and no lock is taken.

Escaping is as parallel as the platform's unwinder and no more. The arm and a C++ throw
walk the same machinery, so as threads are added their per-escape latency rises together;
the `jmpxx-verify unwind-scale` command measures both and reports the ratio.

## Caveats

A typed catch on the escape path transits the escape without entering it on every runtime,
so a frame that catches its own typed exceptions is unaffected. A catch-all is not
portable. How a `catch (...)` interacts with the escape depends on the C++ runtime: it
transits the escape on libcxxrt, terminates the program on libc++abi, and on libstdc++ and
WebAssembly transits when it rethrows with the idiom
`catch (const abi::__forced_unwind&) { throw; }` and otherwise consumes the escape, which
the arm turns into a defined termination. The outcome is loud in every case, never a silent
wrong landing. Keep a catch-all off the escape path, or use a typed catch.

A frame on the escape path marked `noexcept` terminates the unwind at that frame, because
an empty exception specification is a barrier the forced unwind cannot cross. Functions on
the path between an eject and its landing must not be `noexcept`. A function that holds the
landing may be `noexcept`, because the unwind stops there and never crosses it.

A helper that wraps `eject` must not be declared in a way that lets a caller prove it
neither returns nor unwinds. That proof is what lets the optimizer delete the caller's
cleanup landing pads, and the escape then runs no destructors at all from `-O1` upward. The
`[[noreturn]]` attribute alone does not reach this on the compilers in the support matrix,
which keep the pads while the call may still unwind; a `noexcept` or nothrow declaration
does, because it removes the unwinding edge outright. Leave a wrapper ordinary, as
`jmpxx::unwind::eject` itself is.

The sad path costs an unwinder walk and is not for the hottest escape path. The
`jmpxx-verify unwind` command measures the forced-unwind escape distribution beside a C++
throw at the same depth and reports both: the sad path is bounded, and on the machines
measured it is comparable to a throw rather than uniformly faster. The gate the command
enforces is a determinism bound, the ratio of the arm's 99th-percentile latency to the
throw's at the same depth, which a non-deterministic cleanup is shown to exceed. The two
paths are timed back to back in one loop so that scheduling noise on a shared runner
inflates both tails together and cancels in the ratio rather than failing the gate
spuriously.

## Platform support

The arm drives platform machinery whose behavior differs by ABI, so its guarantees are
stated per ABI. The unwind-execution tier checks them, and continuous integration runs
that tier on each reachable ABI.

On the Itanium and DWARF interface, on x86-64, ARM64, and RISC-V, the arm drives
`_Unwind_ForcedUnwind` through libgcc or libunwind. An escape from nine frames runs every
destructor exactly once across GCC and Clang at every optimization level, on x86-64
natively and on ARM64 and RISC-V under emulation. On 32-bit ARM the exception-handling ABI
uses the same interface with an 8-character exception class. The landing identifies its
frame by the carrier's address during the unwind rather than by a frame address read
outside one, because the EHABI unwinder does not provide the latter.

On WebAssembly there is no forced-unwind primitive, so the escape is a typed throw the
virtual machine unwinds to the landing's catch, with the intermediate destructors run as
the compiler's catch-all-and-rethrow clauses. Destructor counts, nesting, the payload
boundary, and typed and cooperative catch-all transit all hold. Two guarantees differ. The
escape is an ordinary catchable exception with no forced-unwind exemption, so a
non-cooperative catch-all that swallows it is caught by the termination backstop rather
than by the platform unwinder. The sad path is whatever the WebAssembly engine charges for a throw
rather than a library-bounded walk.

On Windows the arm runs under native MSVC, which drives the cleanup through an unwinding
`longjmp`. Under the C++ exception model that performs a termination unwind and runs the
destructors of the frames it passes. The unwind-execution tier runs on MSVC and the
catch-all transit holds.

A GCC or Clang toolchain targeting Windows with structured exception handling, which is
what MinGW-w64 uses on x86-64, has no backend: `available()` is false there and use is a
compile-time refusal. Such a toolchain declares `_Unwind_ForcedUnwind`, so the arm would
link, but libgcc's structured-exception unwinder does not implement it. The call returns
end-of-stack immediately without invoking the stop function or running any cleanup, while
an ordinary throw on the same toolchain unwinds correctly. An arm that links and then
cannot escape is worse than one that refuses.

Link-time optimization does not change the guarantee on the Itanium and DWARF ABIs. A
depth-eight escape across translation units runs every destructor exactly once under
`-flto` at each optimization level, with static linking, with section garbage collection,
and under thin and whole-program modes.

On the ARM exception-handling ABI, link-time optimization is unsupported. The compiler
emits a direct unwinder resume in the cleanup landing pad rather than the ABI's own cleanup
exit, and a forced unwind then terminates as the second frame's cleanup finishes; an
ordinary throw survives it. No preprocessor macro identifies the option, so the header
cannot refuse the configuration. Build that ABI without link-time optimization.

On a bare-metal ARM target the arm runs on a freestanding Cortex-M3 with no operating
system, built against newlib and run under a system-mode emulator, where a depth-eight
escape runs every destructor through the EABI forced unwind. Two bare-metal preconditions
follow. The arm uses thread-local storage for its per-thread state, which a bare-metal
runtime must provide, read on the EABI through `__aeabi_read_tp`; a single-threaded target
can answer it with one fixed block. The `.ARM.exidx` unwind index must be kept by the
linker script, or the unwinder finds no tables and the arm cannot run.
