// SPDX-License-Identifier: MIT
// The minimal, freestanding core of jmpxx: the value-or-error transport, the
// minimal error representation, and single-construct propagation.
//
// This is the include boundary. Everything hosted, the diagnostic layer, the interop
// bridges, the reflection layer and the experimental escape, lives under a separate
// header and is never reached from here, which is what keeps the core usable where
// the standard library is not. docs/reference/policies.md states what that promises a
// consumer; tests/scripts/include_boundary_check.sh is what holds it.
#ifndef JMPXX_CORE_HPP
#define JMPXX_CORE_HPP

#include "jmpxx/core/config.hpp"
#include "jmpxx/core/error.hpp"
#include "jmpxx/core/propagation.hpp"
#include "jmpxx/core/transport.hpp"

#endif  // JMPXX_CORE_HPP
