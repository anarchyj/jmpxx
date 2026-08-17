// SPDX-License-Identifier: MIT
// The public surface behind the X11 headers, a third habit again: capitalized words
// that read as type and enumerator names rather than as macros.
#include <X11/Xlib.h>

#if !defined(None) || !defined(Success) || !defined(Bool) || !defined(Status)
#error "the X11 header tree is not on the include path"
#endif

#include "body.hpp"

int main() { return foreign_probe::drive() > 0 ? 0 : 1; }
