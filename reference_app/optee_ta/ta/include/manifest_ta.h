/* SPDX-License-Identifier: MIT */
/* The interface the normal-world client and the trusted application share. */
#ifndef JMPXX_MANIFEST_TA_H
#define JMPXX_MANIFEST_TA_H

/* The trusted application's identity, as OP-TEE addresses it. */
#define TA_MANIFEST_UUID                                   \
  {                                                        \
    0x7b3f9a10, 0x5c62, 0x4d18, {                          \
      0x9e, 0x21, 0x6a, 0x4c, 0x8f, 0x0d, 0x37, 0xb5       \
    }                                                      \
  }

/* Verify a manifest chain held in the first parameter's memory reference, and report the
 * outcome in the second parameter's values: a fault code and the depth it was found at. */
#define TA_MANIFEST_CMD_VERIFY 0

/* Report the library layout and version the secure side was built against, in the first
 * parameter's values, so the normal world can hold what the documentation states against
 * what the secure world actually compiled. value.a packs sizeof(error) and
 * sizeof(result<int, error>); value.b carries the version as major*10000 + minor*100 +
 * patch. */
#define TA_MANIFEST_CMD_LAYOUT 1

#endif /* JMPXX_MANIFEST_TA_H */
