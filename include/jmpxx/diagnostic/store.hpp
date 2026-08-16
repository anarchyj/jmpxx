// SPDX-License-Identifier: MIT
// Out-of-band diagnostic store. A failure's origin and propagation chain live beside
// the in-flight failure rather than inside the transport, so the transport stays
// narrow and intermediate frames never widen to carry context. The arena is per-thread,
// fixed-capacity, allocation-free, and initialized on first use; the exact bounds and
// overflow behavior are in docs/reference/diagnostics.md.
//
// This header is part of the debug-only diagnostic layer. It is compiled only when
// JMPXX_DIAGNOSTICS_ENABLED is on and is reached only through jmpxx/diagnostics.hpp.
#ifndef JMPXX_DIAGNOSTIC_STORE_HPP
#define JMPXX_DIAGNOSTIC_STORE_HPP

#include "jmpxx/core/config.hpp"

#if JMPXX_DIAGNOSTICS_ENABLED

#include "jmpxx/platform/trace.hpp"

#include <cstdint>
#include <source_location>

namespace jmpxx {
namespace diagnostic {

// Capacity bounds. A failure records up to max_chain propagation hops, and a thread
// holds up to max_inflight failures whose landing scope has not yet released them.
// Both are generous for ordinary use and are stated at the surface so the bound is
// honest rather than hidden; exceeding either degrades to a recorded truncation.
inline constexpr int max_chain = 16;
inline constexpr int max_inflight = 16;

namespace detail {

// One failure's out-of-band context: where it began, the sites it propagated
// through, and an optional captured trace. Trivially copyable so the arena holds it
// by value.
struct record {
  std::uint32_t id;
  std::source_location origin;
  std::source_location hops[max_chain];
  std::uint16_t hop_count;
  bool hops_truncated;
#if JMPXX_STACKTRACE
  platform::trace trace;
#endif
};

// A per-thread arena of records with a stack discipline: a record is opened when a
// failure is created in a deep frame and is released when the landing scope that
// owns it truncates the arena back to the depth it snapshotted. Records opened after
// a snapshot sit above it, so truncation reclaims exactly them.
class store {
  record records_[max_inflight];
  int depth_ = 0;
  std::uint32_t next_id_ = 1;  // 0 is reserved to mean "no record"

 public:
  // Open a record for a newly created failure and return its handle. The handle is
  // issued before the capacity check, so a failure arriving with the arena full still
  // gets one; its record is dropped and find() will not resolve it, so the failure
  // carries no context rather than corrupting another record. A handle needs to be
  // distinct only among the records live at one moment, so the counter wraps after
  // 2^32 opens and reuses values retired long before.
  std::uint32_t open(const std::source_location& origin) noexcept {
    std::uint32_t id = next_id_++;
    if (next_id_ == 0) next_id_ = 1;
    if (depth_ >= max_inflight) return id;
    record& r = records_[depth_++];
    r.id = id;
    r.origin = origin;
    r.hop_count = 0;
    r.hops_truncated = false;
#if JMPXX_STACKTRACE
    r.trace = platform::capture(/*skip=*/2);  // skip open() and the capturing ctor
#endif
    return id;
  }

  // Resolve a handle to its live record, or nullptr if it was dropped on overflow or
  // already released by its landing scope. The scan is newest-first and bounded by
  // depth_, so a released record above depth_ is never resolved.
  record* find(std::uint32_t id) noexcept {
    if (id == 0) return nullptr;
    for (int i = depth_ - 1; i >= 0; --i)
      if (records_[i].id == id) return &records_[i];
    return nullptr;
  }

  // Append a propagation hop to a failure's chain, or mark the chain truncated when
  // the bound is reached. A hop on a dropped or released handle is ignored.
  void add_hop(std::uint32_t id, const std::source_location& loc) noexcept {
    record* r = find(id);
    if (!r) return;
    if (r->hop_count >= max_chain) {
      r->hops_truncated = true;
      return;
    }
    r->hops[r->hop_count++] = loc;
  }

  // A landing scope snapshots this on entry.
  [[nodiscard]] int mark() const noexcept { return depth_; }

  // Release every record opened since a snapshot, bounding diagnostic lifetime to
  // the landing scope that took it. A snapshot at or above the current depth is a
  // no-op, so an out-of-order or stale mark cannot grow the arena.
  void truncate(int m) noexcept {
    if (m >= 0 && m < depth_) depth_ = m;
  }
};

// One arena per thread per program. The function-local thread_local is initialized
// on first use, so a failure produced before main initializes the arena on the
// thread that produced it without any static-initialization-order dependency.
inline store& tls() noexcept {
  static thread_local store s;
  return s;
}

}  // namespace detail
}  // namespace diagnostic
}  // namespace jmpxx

#endif  // JMPXX_DIAGNOSTICS_ENABLED
#endif  // JMPXX_DIAGNOSTIC_STORE_HPP
