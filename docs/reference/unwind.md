<!-- SPDX-License-Identifier: MIT -->
# Experimental non-local unwind arm

The unwind arm returns a failure from arbitrary call depth to a single landing boundary
while the platform unwinder runs the destructor of every automatic object on the path, so
the frames in between carry no propagation construct in their source. It is experimental,
platform-specific, and opt-in: a program reaches it only by including `jmpxx/unwind.hpp`.

This page is the contract. How the mechanism works, what each guarantee was measured on,
and what is still unproven are in [the design note](../design/unwind-arm.md).

The arm is not a replacement for the portable surface. `result` and `JMPXX_TRY` need no
unwind tables and impose no obligations on the frames between a failure and its handler,
and they remain the right choice for strict embedded and low-latency code. Reach for the
arm when you can afford cleanup tables and want the middle frames to say nothing at all.

## Surface (`jmpxx/unwind.hpp`)

`jmpxx::unwind::escape_scope<E>(body)` runs `body` within a landing boundary and returns a
`result<T, E>`, with `T` deduced from `body`'s return type and `E` defaulting to
`jmpxx::error`. If `body` returns normally its value becomes the result's value. If any
frame it transitively calls ejects, the stack unwinds back to this scope and the result
holds that error. Scopes nest, and an eject lands at the nearest enclosing scope. The
landing allocates nothing.

`jmpxx::unwind::eject(err)` escapes to the innermost `escape_scope` on the current thread,
carrying `err`. It does not return at run time. It is deliberately not `[[noreturn]]`, and
it returns a type convertible to anything, so `return jmpxx::unwind::eject(err);` type-checks
in a function of any return type.

`jmpxx::unwind::available()` is a `constexpr bool`, false on a target with no backend, where
`escape_scope` and `eject` refuse instantiation with a stated precondition. Branch on it
with `if constexpr`, or guard the including translation unit with `JMPXX_UNWIND_AVAILABLE`;
a runtime `if` does not prevent the instantiation.

## Preconditions

The error type must match the receiving scope and must be small and trivially copyable,
which every error representation the library ships satisfies. Richer context travels out of
band, as the resolver in the reference application does by stashing the offending key before
it ejects.

Every frame between an eject and its landing must be compiled with unwind cleanup tables,
which is the effect of compiling with exceptions enabled. A translation unit that includes
`jmpxx/unwind.hpp` with exceptions disabled is refused at compile time rather than silently
producing an arm that skips destructors. That refusal covers only the translation unit that
opts in. A frame elsewhere on the escape path compiled `-fno-exceptions` has its destructors
skipped, and the header cannot see it.

No frame between an eject and its landing may be `noexcept`. An empty exception specification
is a barrier the unwind cannot cross and the program terminates there. A function that holds
the landing may be `noexcept`, because the unwind stops at it and never crosses it.

A helper that wraps `eject` must not be declared in a way that lets its caller prove it
neither returns nor unwinds. That proof lets the optimizer delete the caller's cleanup
landing pads, after which the escape runs no destructors at all. Leave such a wrapper
ordinary, as `jmpxx::unwind::eject` itself is.

## Failure modes

Each of these is a defined termination with its own diagnostic, not undefined behavior:

- No `escape_scope` is active on this thread.
- The ejected error type does not match the active scope's.
- The active scope has already returned, or belongs to another thread.
- A second escape starts while one is already unwinding on this thread, whether it is aimed
  at the scope already unwinding or at a fresh scope a destructor opened as it ran as
  cleanup. A destructor running as cleanup may open a scope and use it, as long as nothing
  escapes from it while the outer escape is in flight.
- A non-cooperative `catch (...)` on the path swallows the escape, so it never reaches its
  landing.
- The platform could not unwind at all, which is what a configuration missing cleanup tables
  produces.

## Handlers on the escape path

A typed catch transits the escape on every runtime, so a frame that catches its own typed
exceptions is unaffected. A catch-all is not portable: what it does to the escape is the C++
runtime's decision, and the outcomes range from transit to termination. On libcxxrt, the C++
runtime on FreeBSD, a catch-all and an empty exception specification are worse than
unportable: the frame is skipped without running its destructors and the landing still
succeeds, so the caller sees a success and a leak, and nothing in the arm can detect it.

Keep a catch-all off the escape path, or use a typed catch. The per-runtime table and its
provenance are in [the design note](../design/unwind-arm.md).

## Cost and threads

The sad path costs an unwinder walk and is not for the hottest escape path. `jmpxx-verify
unwind` measures the escape distribution beside a C++ throw at the same depth and reports
both; the gate is a determinism bound on the ratio of their high percentiles rather than an
absolute time, so the two inflate together under load and the ratio still holds.

Landing scopes are per thread. Each thread has its own stack of active scopes and its own
in-flight state, nothing is shared, and no lock is taken. Escaping is as parallel as the
platform's unwinder and no more, which `jmpxx-verify unwind-scale` measures against a throw
in the same loop.

## Platform support

| target | arm | note |
| --- | --- | --- |
| Itanium and DWARF on x86-64, ARM64, RISC-V | supported | `_Unwind_ForcedUnwind` through libgcc or libunwind |
| ARM exception-handling ABI, 32-bit and bare metal | supported | not with link-time optimization; see below |
| WebAssembly | supported | two guarantees differ; see below |
| Windows, native MSVC | supported | an unwinding `longjmp` under the C++ exception model |
| Windows, GCC or Clang with structured exception handling | no backend | `available()` is false and use is a compile-time refusal |

On WebAssembly the escape is an ordinary catchable exception with no forced-unwind
exemption, so a non-cooperative catch-all that swallows it is caught by the arm's own
termination backstop rather than by the platform unwinder, and the sad path is whatever the
engine charges for a throw rather than a library-bounded walk.

On the ARM exception-handling ABI, link-time optimization is unsupported: a forced unwind
terminates at the second frame's cleanup. No preprocessor macro identifies the option, so
the header cannot refuse it. Build that ABI without link-time optimization.

On a bare-metal ARM target the arm needs two things from the runtime. Thread-local storage
must be available for its per-thread state, read on that ABI through `__aeabi_read_tp`; a
single-threaded target can answer with one fixed block. The unwind index section must be
kept by the linker script, or the unwinder finds no tables and the arm cannot run.

## Status

Experimental. The arm is exempt from the layout-stability promise, its surface may change
between minor versions, and it is not recommended for a stable support contract yet. [The
design note](../design/unwind-arm.md) states what remains unproven.
