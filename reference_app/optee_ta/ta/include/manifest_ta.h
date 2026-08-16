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

#endif /* JMPXX_MANIFEST_TA_H */
