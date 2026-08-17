// SPDX-License-Identifier: MIT
// Fenced fail-fast. Terminating the program is the one platform-specific act the
// core performs, so it lives behind this boundary rather than scattered through the
// core; docs/reference/platform.md states what a caller sees when it fires. The
// textual reason is accepted now and reserved for a hosted diagnostic sink; today
// the trap itself is the diagnostic, deterministic and visible to a debugger.
#ifndef JMPXX_PLATFORM_TRAP_HPP
#define JMPXX_PLATFORM_TRAP_HPP

#include "jmpxx/core/config.hpp"

namespace jmpxx {
namespace platform {

// Terminate immediately and never return. A compiler intrinsic rather than abort()
// so the core needs no hosted header and the stop is one instruction a debugger
// lands on.
[[noreturn]] JMPXX_ALWAYS_INLINE void fail_fast(const char* reason) noexcept {
  (void)reason;
#if JMPXX_HAS_BUILTIN(__builtin_trap) || JMPXX_COMPILER_GCC || JMPXX_COMPILER_CLANG
  __builtin_trap();
#elif JMPXX_COMPILER_MSVC
  __debugbreak();
#endif
  // Satisfy [[noreturn]] on any path where the intrinsic above is not itself
  // marked noreturn (for example MSVC's __debugbreak).
  for (;;) {
  }
}

}  // namespace platform
}  // namespace jmpxx

#endif  // JMPXX_PLATFORM_TRAP_HPP
