// SPDX-License-Identifier: MIT
// The public surface behind the Windows headers. This ecosystem's collision habit is
// the opposite of the firmware one: ordinary English words taken as macros, in the
// lower case a C++ identifier uses.
#include <windows.h>

#if !defined(near) || !defined(far) || !defined(interface) || !defined(ERROR)
#error "the Windows header tree is not on the include path"
#endif

#include "body.hpp"

int main() { return foreign_probe::drive() > 0 ? 0 : 1; }
