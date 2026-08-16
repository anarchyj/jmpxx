// SPDX-License-Identifier: MIT
// The propagation diagnostic hook. Single-construct propagation calls
// note_propagation on the failure path so a hosted diagnostic layer can record a
// causal hop as a failure travels toward its landing boundary. The core carries the
// no-op default and jmpxx/diagnostics.hpp contributes the recording overload.
#ifndef JMPXX_CORE_DIAGNOSTIC_HOOK_HPP
#define JMPXX_CORE_DIAGNOSTIC_HOOK_HPP

#include "jmpxx/core/config.hpp"

namespace jmpxx {
namespace detail {

// A more specialized non-template overload, contributed by a richer policy, wins
// over this template for that policy's error type.
template <class E>
JMPXX_ALWAYS_INLINE constexpr void note_propagation(const E&) noexcept {}

}  // namespace detail
}  // namespace jmpxx

#endif  // JMPXX_CORE_DIAGNOSTIC_HOOK_HPP
