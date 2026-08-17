// SPDX-License-Identifier: MIT
// The rich default policy (rich_error) and its debug-only diagnostic layer.
//
// rich_error carries the same in-band code and domain as the minimal error; in a debug
// build it also tags the failure with a handle into a per-thread out-of-band store
// holding the origin and the causal chain, and in a release build it compiles down to
// exactly the minimal error. That release identity is the dual-personality guarantee,
// verified by the release codegen diff, not asserted here. The debug and release
// behavior, the lifetime rules, and the concurrency contract are in
// docs/reference/diagnostics.md.
//
// Hosted extension: jmpxx/core.hpp never pulls it in, and call sites select it only by
// choosing the error type. <source_location> and the origin-capturing machinery are
// named only under JMPXX_DIAGNOSTICS_ENABLED, because a source location materializes
// its file and function strings into the binary even where the value is unused.
#ifndef JMPXX_DIAGNOSTICS_HPP
#define JMPXX_DIAGNOSTICS_HPP

// The full minimal core, so the rich policy is usable from this header alone:
// result, fail, the propagation macros, the minimal error, and the hop hook the
// rich overload below specializes.
#include "jmpxx/core.hpp"

#include <cstdint>
#include <cstdio>

#if JMPXX_DIAGNOSTICS_ENABLED
#include "jmpxx/diagnostic/store.hpp"
#include <source_location>
#endif

namespace jmpxx {

// The rich default error representation. Trivially copyable in every build so the
// transport that carries it stays register-passable and allocation-free. In debug
// it adds one handle word that ties the failure to its out-of-band context; in
// release it is exactly the minimal error.
//
// Failure modes and concurrency: a rich_error never throws and never allocates. Its
// origin and chain live in the thread-local store, so a rich_error read from the
// thread that produced it resolves its context, while one moved to another thread
// resolves to no context rather than to another thread's, which is what keeps the
// layer race-free. Reading the context is valid only while the owning landing scope
// is alive; see landing and diagnostic::inspect.
class rich_error {
 public:
  // The in-band payload, named and laid out exactly as the minimal error, so a
  // landing that reads err.code and err.domain reads the same source under either
  // policy. The field names keep one source usable under every shipped policy.
  int code = 0;
  int domain = 0;

 private:
#if JMPXX_DIAGNOSTICS_ENABLED
  std::uint32_t origin_ = 0;  // handle into the thread-local store; 0 == none
#endif

 public:
  constexpr rich_error() noexcept = default;
  rich_error(const rich_error&) noexcept = default;
  rich_error(rich_error&&) noexcept = default;
  rich_error& operator=(const rich_error&) noexcept = default;
  rich_error& operator=(rich_error&&) noexcept = default;

#if JMPXX_DIAGNOSTICS_ENABLED
  // The location defaults to the construction site, so a failure site that writes
  // rich_error(code, domain) captures where it failed without naming a location.
  // These touch the thread-local store, so they are not constexpr; the constexpr
  // surface is the release representation below.
  explicit rich_error(
      int c, int d = 0,
      std::source_location loc = std::source_location::current()) noexcept
      : code(c), domain(d), origin_(diagnostic::detail::tls().open(loc)) {}
  rich_error(error e,
             std::source_location loc = std::source_location::current()) noexcept
      : code(e.code), domain(e.domain), origin_(diagnostic::detail::tls().open(loc)) {}

  // The handle the propagation hook uses to attach a hop. Not part of the stable
  // surface; it exists only in debug.
  [[nodiscard]] std::uint32_t handle() const noexcept { return origin_; }
#else
  constexpr explicit rich_error(int c, int d = 0) noexcept : code(c), domain(d) {}
  constexpr rich_error(error e) noexcept : code(e.code), domain(e.domain) {}
#endif

  // Decay to the minimal error, carrying the in-band code and domain. Explicit so a
  // rich_error is never silently narrowed where the rich type was intended.
  [[nodiscard]] constexpr error base() const noexcept { return error(code, domain); }
  [[nodiscard]] constexpr explicit operator error() const noexcept {
    return error(code, domain);
  }

  // Equal when the in-band code and domain match. The out-of-band context is
  // per-occurrence and is not part of value identity.
  [[nodiscard]] friend constexpr bool operator==(rich_error a,
                                                  rich_error b) noexcept {
    return a.code == b.code && a.domain == b.domain;
  }
};

static_assert(__is_trivially_copyable(rich_error),
              "rich_error must stay trivially copyable so the transport that "
              "carries it stays register-passable; the diagnostic context is held "
              "out of band, never in the error");

// A landing scope owns the diagnostic context of every failure created under it.
// The lifetime contract a caller needs is in docs/reference/diagnostics.md; what the
// code cannot show is why the bound is a store mark rather than a count of records:
// releasing to a mark makes nesting free and makes an early return release exactly
// what the scope added, with no bookkeeping per failure.
class landing {
#if JMPXX_DIAGNOSTICS_ENABLED
  int mark_;

 public:
  landing() noexcept : mark_(diagnostic::detail::tls().mark()) {}
  ~landing() { diagnostic::detail::tls().truncate(mark_); }
#else
 public:
  constexpr landing() noexcept = default;
#endif
  landing(const landing&) = delete;
  landing& operator=(const landing&) = delete;
};

namespace detail {
#if JMPXX_DIAGNOSTICS_ENABLED
// The rich-policy overload of the propagation hook. The single-construct
// propagation macros call note_propagation on the failure path; for rich_error this
// records the propagation site as a hop, capturing the call-site location through
// the defaulted argument. Selected over the core's no-op template by overload
// resolution, and present only in debug, so the minimal policy and every release
// build record nothing.
JMPXX_ALWAYS_INLINE void note_propagation(
    const rich_error& e,
    std::source_location loc = std::source_location::current()) noexcept {
  diagnostic::detail::tls().add_hop(e.handle(), loc);
}
#endif
}  // namespace detail

namespace diagnostic {

#if JMPXX_DIAGNOSTICS_ENABLED
// A read-only view of a failure's captured context. The validity window, the meaning
// of each field, and what a caller may not retain are in
// docs/reference/diagnostics.md. The view is a borrowed pointer rather than a copy so
// that inspecting a failure allocates nothing and copies no chain.
struct context {
  bool available;
  std::source_location origin;
  const std::source_location* hops;
  int hop_count;
  bool hops_truncated;
  platform::trace trace;
  bool trace_captured;
};

// Resolve a rich error's out-of-band context on the calling thread.
[[nodiscard]] inline context inspect(const rich_error& e) noexcept {
  context c{};
  detail::record* r = detail::tls().find(e.handle());
  if (!r) {
    c.available = false;
    return c;
  }
  c.available = true;
  c.origin = r->origin;
  c.hops = r->hops;
  c.hop_count = r->hop_count;
  c.hops_truncated = r->hops_truncated;
#if JMPXX_STACKTRACE
  c.trace = r->trace;
  c.trace_captured = !r->trace.empty();
#else
  c.trace = {};
  c.trace_captured = false;
#endif
  return c;
}

// The number of propagation hops recorded for a failure, or 0 if none.
[[nodiscard]] inline int chain_length(const rich_error& e) noexcept {
  detail::record* r = detail::tls().find(e.handle());
  return r ? r->hop_count : 0;
}
#endif  // JMPXX_DIAGNOSTICS_ENABLED

// Write a failure's origin and causal chain to out, one frame per line. In debug it
// renders the captured context; in release it is a no-op, because the context does
// not exist. A program calls this the same way in both builds, and the release call
// compiles to nothing.
inline void print(const rich_error& e, std::FILE* out) noexcept {
#if JMPXX_DIAGNOSTICS_ENABLED
  context c = inspect(e);
  if (!c.available) {
    std::fprintf(out, "  (no diagnostic context: code=%d domain=%d)\n", e.code,
                 e.domain);
    return;
  }
  std::fprintf(out, "  origin: %s:%u  %s\n", c.origin.file_name(),
               c.origin.line(), c.origin.function_name());
  for (int i = 0; i < c.hop_count; ++i)
    std::fprintf(out, "  via:    %s:%u  %s\n", c.hops[i].file_name(),
                 c.hops[i].line(), c.hops[i].function_name());
  if (c.hops_truncated) std::fprintf(out, "  ... (chain truncated)\n");
  if (c.trace_captured)
    std::fprintf(out, "  trace:  %d raw frame%s\n", c.trace.count,
                 c.trace.count == 1 ? "" : "s");
#else
  (void)e;
  (void)out;
#endif
}

}  // namespace diagnostic
}  // namespace jmpxx

#endif  // JMPXX_DIAGNOSTICS_HPP
