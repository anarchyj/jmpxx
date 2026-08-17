<!-- SPDX-License-Identifier: MIT -->
# Diagnostic layer

The diagnostic layer attaches a failure's origin and the causal chain it accumulates
as it propagates, held out of band so the transport stays narrow. It is part of the
rich policy ([policies.md](policies.md)) and is reached by including
`jmpxx/diagnostics.hpp`. It is rich in a debug build and compiles to nothing in a
release build. Including `jmpxx/core.hpp` never pulls it in, which the
`include_boundary` test confirms by scanning the minimal core's include graph.

## Debug and release personalities

The layer is on when `JMPXX_DIAGNOSTICS_ENABLED` is set, which defaults to on unless
`NDEBUG` is defined. When it is off, `rich_error` is layout- and codegen-identical to
`jmpxx::error`, `<source_location>` is not included, and every facility below is
compiled out.

Compiled out means the names are gone, not that the calls become no-ops. `landing` stays
and becomes an empty object, so a scope written for the debug build still compiles; the
context surface does not. Code that calls `diagnostic::inspect`, names
`diagnostic::context`, or reads a `chain_length` guards those uses with
`#if JMPXX_DIAGNOSTICS_ENABLED`, or asks for the layer explicitly with
`-DJMPXX_DIAGNOSTICS_ENABLED=1`, which is independent of `NDEBUG`. A host build that
defines `NDEBUG` for its own reasons is the common way to meet this. The release identity
is enforced, not assumed: `jmpxx-verify release-diff` requires the rich and minimal
policies to generate identical code in a release build, and fails if a diagnostic call
or a source-location string reaches
release read-only data, because a source location materializes its file and function
strings into the binary even where the value is unused.

## Capturing context

A `rich_error` constructed from a code captures its construction site as the origin,
through a defaulted `std::source_location` argument, so a failure site that writes
`rich_error(code, domain)` records where it failed without naming a location. As the
failure propagates, each `JMPXX_TRY`, `JMPXX_TRYV`, and `JMPXX_TRYX` on its path
records the propagation site as one hop. The minimal policy and every release build
record nothing, because the propagation macros expand the hook to nothing there.

## Capturing the origin at a bridge

The origin is wherever the `rich_error` is constructed, because the location is a
defaulted `std::source_location` resolved at that point. When a foreign error is
bridged into a failure, a `VkResult` from a graphics call or an `errno` from the
filesystem, construct the `rich_error` at the call site so the origin points at the
caller. A macro does this; a shared helper function does not, because the helper
captures its own location rather than the caller's. When a function is preferred over
a macro, give it a trailing `std::source_location loc = std::source_location::current()`
parameter and forward it to the `rich_error` constructor, which preserves the caller's
location:

```cpp
inline jmpxx::failure<jmpxx::rich_error> fail_vk(
    VkResult r, std::source_location loc = std::source_location::current()) {
  return jmpxx::fail(jmpxx::rich_error(static_cast<int>(r), vk_domain, loc));
}
```

## `jmpxx::landing`

A scope that owns the diagnostic context of every failure created under it. It
snapshots the store on construction and releases back to that snapshot on
destruction, so no diagnostic context outlives the scope that handles its failure,
and nested scopes nest correctly. A program places one where it handles failures and
reads a failure's context while it is alive. In a release build it is an empty
object. It is not copyable.

Reading a context after its landing scope has exited returns no context rather than
stale data, because the released record is no longer resolvable by its handle. The
`policy.sanitizer_address` test runs landing-scope exit under the address and
undefined-behavior sanitizers.

## Reading context

`jmpxx::diagnostic::inspect(const rich_error&)` returns a `context` valid only while
the owning landing scope is alive. Its `available` field is false when the handle was
dropped on overflow or already released, in which case the failure carries no
context. When available it exposes the `origin`, a pointer to `hop_count` hops,
`hops_truncated`, a raw `platform::trace`, and `trace_captured`. The pointers alias
the per-thread store and must not be retained past the landing scope.
`jmpxx::diagnostic::chain_length(const rich_error&)` returns the hop count alone.
`jmpxx::diagnostic::print(const rich_error&, std::FILE*)` writes the origin and each
hop one per line, prints the raw frame count when a trace was captured, and is a
no-op in a release build.

## Concurrency

The store is per-thread, so concurrent failures on different threads never touch the
same memory and there is no data race. A `rich_error` read on the thread that created
it resolves its context; one moved to another thread resolves to no context rather
than to another thread's, so a context is never read across threads. The
`policy.sanitizer_thread` test drives concurrent failures on eight threads under the
thread sanitizer. Diagnostic capture takes no lock and is not async-signal-safe.

## Bounds

The store is a fixed per-thread arena, so capture allocates nothing, which the
`alloc` command confirms for the rich policy. A failure records up to `max_chain`
hops and a thread holds up to `max_inflight` failures whose landing scope has not
released them, both 16. A chain beyond the hop bound sets `hops_truncated`; a failure
beyond the in-flight bound carries no context. The bound is the cost of the no-heap
guarantee.

## Optional trace

When `JMPXX_STACKTRACE` is set, capture also records the return addresses of the
creation site, on a platform where a fenced capturer is available, behind
`jmpxx/platform/trace.hpp`. It is off by default, captures addresses only without
symbolizing, takes no lock, and allocates nothing. A platform without a fenced
capturer returns an empty trace rather than an unverified one;
`jmpxx::platform::trace_available()` reports which. `inspect` exposes the raw trace
and `print` reports the captured frame count. Symbolizing the addresses is a separate,
offline concern.
