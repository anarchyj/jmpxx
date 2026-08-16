/* SPDX-License-Identifier: MIT */
#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include "manifest_ta.h"

#define TA_UUID TA_MANIFEST_UUID

/* Single instance, multi session, and kept alive so repeated verifications reuse the same
 * instance. That is what makes the reuse property this trusted application demonstrates
 * observable: the state a failed verification leaves behind is the state the next one
 * starts from. */
#define TA_FLAGS (TA_FLAG_SINGLE_INSTANCE | TA_FLAG_MULTI_SESSION | \
                  TA_FLAG_INSTANCE_KEEP_ALIVE)

/* The unwinder walks frames and the parser recurses, so the stack is sized for the
 * declared nesting limit rather than the default. */
#define TA_STACK_SIZE (16 * 1024)
#define TA_DATA_SIZE (32 * 1024)

#define TA_CURRENT_TA_EXT_PROPERTIES \
  { "gp.ta.description", USER_TA_PROP_TYPE_STRING, "jmpxx manifest verifier" }

#endif /* USER_TA_HEADER_DEFINES_H */
