/* SPDX-License-Identifier: MIT
 *
 * A trusted application builds without a hosted C library, and libstdc++'s
 * aarch64-none-linux-gnu configuration header includes <features.h> before anything else
 * and then asks __GLIBC_PREREQ which glibc it is talking to. There is no glibc here, so
 * this shim answers the one question that gets asked: no version is at least any version.
 *
 * Only the freestanding headers the jmpxx core uses come through this path, <type_traits>
 * and <cstddef>, and neither reaches a glibc facility. A hosted header would need far more
 * than this shim and does not belong in a trusted application in the first place.
 *
 * Using the bare-metal aarch64-none-elf toolchain instead would avoid the shim, at the
 * cost of a second toolchain for one translation unit. The shim is smaller and its scope
 * is visible.
 */
#ifndef JMPXX_TA_FEATURES_H
#define JMPXX_TA_FEATURES_H

#define __GLIBC_PREREQ(maj, min) 0

#endif /* JMPXX_TA_FEATURES_H */
