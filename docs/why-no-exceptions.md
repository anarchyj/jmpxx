<!-- SPDX-License-Identifier: MIT -->
# Why exceptions get disabled

A large part of the C++ world builds with `-fno-exceptions`. Embedded firmware, game
engines, robotics, real-time media, trading systems, simulation engines, networking and
storage infrastructure, database internals, and WebAssembly runtimes all turn
exceptions off, and they do it for reasons that are about cost and predictability rather
than taste. Those reasons are what jmpxx is built for.

## The costs they are avoiding

Exceptions impose three costs these systems cannot always pay.

The sad path is not deterministic. Throwing drives the unwinder, which searches for a
handler frame by frame and runs each frame's cleanup. The time that takes depends on how
far the throw travels and what it passes through, and its tail is wide. Code with a hard
latency bound, a trading path that must answer in microseconds, an audio callback that
must never miss its deadline, a control loop on a robot, cannot accept a failure path
whose cost it cannot state.

The unwinder needs tables. Exceptions require unwind tables in the binary so the runtime
can walk the stack. On a target counting kilobytes, a microcontroller or a binary shipped
to millions of devices, those tables are size the system does not have to spend, which is
why the toolchain is told to omit them.

The generated code has to be inspectable. Engineers in these domains read the assembly
their code produces and budget for it. A mechanism whose cost they cannot see in the
generated code is a mechanism they cannot adopt.

## What they do instead, and what it costs them

With exceptions off, a failure deep in a call chain is returned by hand. Every function
on the path grows a status return or an out-parameter, every caller writes
`if (failed) return the_failure;`, and the error type threads through functions that have
nothing to do with the error except passing it along. It is correct and predictable, and
it is verbose and fragile: a dropped status, a forgotten check, an error type
widened through a dozen signatures that never look at it.

## Where jmpxx fits

jmpxx removes the manual threading while keeping the properties no-exceptions code
depends on. A failure returns through the chain as an ordinary value, so the sad path is a
branch with a cost you can read, not an unwinder walk. Each frame forwards it with one
construct instead of the check-and-return you would otherwise write, and the context the
failure accumulates travels out of band so the returned value stays narrow. Destructors
still run, because the value returns normally through each frame.
The cost is measured rather than asserted; see [comparison.md](comparison.md) for the
numbers, the gates behind them, and where jmpxx does not win.

For code that can afford unwind tables and wants the intermediate frames to carry no
propagation construct at all, the experimental [unwind arm](reference/unwind.md) offers
the closest standard-C++ approach to a non-local jump that still runs destructors, with a
sad-path cost it measures rather than hides. It is opt-in and never the default. The
portable surface above is the right choice for strict no-exceptions code.

## Not a replacement for exceptions everywhere

jmpxx is for recoverable, expected failures on a path that has chosen to forgo
exceptions. It is not a way to turn exceptions back on, and it does not handle
programming bugs: a precondition violation or a broken invariant is a bug for a contract
or a termination to catch, not a failure to propagate. Where exceptions are enabled and
appropriate, an [interop bridge](reference/interop.md) crosses between a propagated
failure and a thrown one at the boundary.
