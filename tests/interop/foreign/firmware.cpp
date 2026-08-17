// SPDX-License-Identifier: MIT
// The public surface behind the Trusted Firmware header tree, in the order a firmware
// consumer produces: their headers first, jmpxx after. That order is what made the
// transport's monadic alias collide with the U(v) macro this tree defines, and no
// locally written program could have reached it.
#include <lib/utils_def.h>

// The tree has to be on the include path for this fixture to mean anything. A tier
// that passes because the headers were not there is a tier that checked nothing.
#if !defined(U) || !defined(BIT) || !defined(ARRAY_SIZE)
#error "the Trusted Firmware header tree is not on the include path"
#endif

#include "body.hpp"

int main() { return foreign_probe::drive() > 0 ? 0 : 1; }
