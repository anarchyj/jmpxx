// SPDX-License-Identifier: MIT
// The public surface behind the Zephyr headers. It shares the firmware habit of short
// uppercase macros and spells a different set of them, so it covers names the Trusted
// Firmware tree does not.
#include <zephyr/sys/util_macro.h>

#if !defined(BIT) || !defined(IS_ENABLED) || !defined(COND_CODE_1)
#error "the Zephyr header tree is not on the include path"
#endif

#include "body.hpp"

int main() { return foreign_probe::drive() > 0 ? 0 : 1; }
