/* SPDX-License-Identifier: MIT
 *
 * The normal-world half of the manifest verifier: it builds manifests, hands them to the
 * trusted application over the client API, and checks what comes back.
 *
 * This side is plain C on purpose. The library under test lives in the secure world, and
 * keeping the client free of it makes the dependency direction obvious: the verdicts below
 * are produced by jmpxx running inside OP-TEE, not by anything linked here.
 *
 * What the checks are really for. Each bad manifest is a different way to fail at a
 * different depth, and every one is followed by a good manifest through the same
 * long-lived trusted-application instance. A secure-world service is not restarted between
 * requests, so a failed request that leaked a hash context or left its visit path
 * populated would corrupt the next request. That is the property the unwind arm provides
 * and a bare longjmp would not, and the only way to see it is to make the requests in
 * sequence against one instance, which is what this does.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <tee_client_api.h>

#include "manifest_ta.h"

#define DIGEST_BYTES 32

/* The faults the trusted application reports, mirrored so the output names them. */
static const char *fault_text(uint32_t f) {
  switch (f) {
    case 0: return "verified";
    case 1: return "the manifest is truncated";
    case 2: return "the header is not MFST";
    case 3: return "the schema version is unsupported";
    case 4: return "an entry is malformed";
    case 5: return "an image length is implausible";
    case 6: return "a digest does not match";
    case 7: return "a referenced entry is absent";
    case 8: return "the references form a cycle";
    case 9: return "the reference nesting is too deep";
    case 10: return "a digest could not be started";
  }
  return "an unknown fault";
}

/* A manifest under construction. The layout matches what the trusted application parses:
 *   "MFST" version(1) count(1)
 *   per entry: name_len(1) name length(4, little endian) digest[32] ref_len(1) ref
 *              image[length]
 * Each entry carries its own image inline, so an entry verifies wherever it sits. */
struct builder {
  uint8_t buf[4096];
  size_t len;
};

static void put(struct builder *b, const void *p, size_t n) {
  memcpy(b->buf + b->len, p, n);
  b->len += n;
}
static void put_u8(struct builder *b, uint8_t v) { put(b, &v, 1); }
static void put_u32(struct builder *b, uint32_t v) {
  uint8_t raw[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  put(b, raw, 4);
}

static void begin(struct builder *b, uint8_t count, const char *magic, uint8_t version) {
  b->len = 0;
  put(b, magic, 4);
  put_u8(b, version);
  put_u8(b, count);
}

/* One entry. The digest is supplied rather than computed, so a test can hand over a wrong
 * one deliberately; the correct digests come from the table below. */
static void entry(struct builder *b, const char *name, uint32_t length,
                  const uint8_t digest[DIGEST_BYTES], const char *ref);

/* SHA-256 of the four-byte image "IMG0", which every entry below declares as its content.
 * A real boot stage reads the image from flash and hashes it; here the same four bytes
 * follow the manifest so the secure world hashes exactly what the client declared. */
static const uint8_t digest_img0[DIGEST_BYTES] = {
    0x15, 0x35, 0x70, 0xdf, 0x59, 0x72, 0x40, 0xec,
    0x00, 0xb9, 0xbc, 0x2c, 0xf6, 0x8a, 0x63, 0x58,
    0x1f, 0x84, 0x4e, 0x81, 0x25, 0xbe, 0x64, 0x66,
    0x17, 0x9e, 0xc7, 0x93, 0x24, 0x38, 0xbb, 0x16};
static const uint8_t digest_wrong[DIGEST_BYTES] = {0xde, 0xad, 0xbe, 0xef};
static const char image[] = "IMG0";

static void entry(struct builder *b, const char *name, uint32_t length,
                  const uint8_t digest[DIGEST_BYTES], const char *ref) {
  put_u8(b, (uint8_t)strlen(name));
  put(b, name, strlen(name));
  put_u32(b, length);
  put(b, digest, DIGEST_BYTES);
  put_u8(b, ref ? (uint8_t)strlen(ref) : 0);
  if (ref) put(b, ref, strlen(ref));
  put(b, image, length);
}

static int failures;

static void expect(int ok, const char *what) {
  if (!ok) {
    printf("  FAIL: %s\n", what);
    ++failures;
  }
}

/* Invoke the trusted application and print what it reported.
 *
 * A session is opened per manifest rather than once for the run. That is not tidiness: the
 * verifier keeps its landing state in thread-local storage, and this OP-TEE build does not
 * establish a trusted application's thread-local block for every TEE thread that enters
 * it, so a later invocation on a different thread reads a landing pointer that is not one.
 * Opening a session per invocation keeps each one on a thread whose block is initialized.
 * The finding is recorded in the field report; this is the shape it forces on a caller.
 */
static TEEC_Context g_ctx;

static void run(const char *label, struct builder *b, uint32_t want_fault) {
  TEEC_Session session;
  TEEC_UUID uuid = TA_MANIFEST_UUID;
  TEEC_Operation op;
  uint8_t name_out[64];
  uint32_t origin = 0;

  if (TEEC_OpenSession(&g_ctx, &session, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL,
                       &origin) != TEEC_SUCCESS) {
    printf("  %-12s session did not open (origin 0x%x)\n", label, origin);
    ++failures;
    return;
  }

  memset(&op, 0, sizeof(op));
  op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_OUTPUT,
                                   TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE);
  op.params[0].tmpref.buffer = b->buf;
  op.params[0].tmpref.size = b->len;
  op.params[2].tmpref.buffer = name_out;
  op.params[2].tmpref.size = sizeof(name_out);

  TEEC_Result res = TEEC_InvokeCommand(&session, TA_MANIFEST_CMD_VERIFY, &op, &origin);
  TEEC_CloseSession(&session);
  if (res != TEEC_SUCCESS) {
    printf("  %-12s invoke failed: 0x%x origin 0x%x\n", label, res, origin);
    ++failures;
    return;
  }

  const uint32_t fault = op.params[1].value.a;
  const uint32_t detail = op.params[1].value.b;
  const size_t out_len = op.params[2].tmpref.size;
  const uint8_t path_left = out_len >= 2 ? name_out[out_len - 1] : 0xFF;
  const char *name = (const char *)name_out;

  if (fault == 0)
    printf("  %-12s verified, %u entr%s\n", label, detail, detail == 1 ? "y" : "ies");
  else
    printf("  %-12s fault %u at depth %u in '%s': %s\n", label, fault, detail, name,
           fault_text(fault));

  expect(fault == want_fault, "the reported fault is the expected one");
  /* The reuse property: the secure-world instance left no reference path behind, so the
   * next request starts clean. */
  expect(path_left == 0, "the trusted application left its reference path empty");
}

int main(void) {
  struct builder b;

  printf("verify_client: driving the manifest verifier in the secure world\n");
  if (TEEC_InitializeContext(NULL, &g_ctx) != TEEC_SUCCESS) {
    printf("verify_client: no TEE context; is OP-TEE running?\n");
    return 1;
  }

  /* A chain three deep that verifies. */
  begin(&b, 3, "MFST", 1);
  entry(&b, "boot", sizeof(image) - 1, digest_img0, "runtime");
  entry(&b, "runtime", sizeof(image) - 1, digest_img0, "app");
  entry(&b, "app", sizeof(image) - 1, digest_img0, NULL);
  run("valid", &b, 0);

  /* The deepest entry's digest is wrong, so the fault is found below two good frames. */
  begin(&b, 3, "MFST", 1);
  entry(&b, "boot", sizeof(image) - 1, digest_img0, "runtime");
  entry(&b, "runtime", sizeof(image) - 1, digest_img0, "app");
  entry(&b, "app", sizeof(image) - 1, digest_wrong, NULL);
  run("bad digest", &b, 6);

  /* Immediately afterwards, the same instance verifies a good manifest again. This is the
   * check that the failed request released everything it held. */
  begin(&b, 3, "MFST", 1);
  entry(&b, "boot", sizeof(image) - 1, digest_img0, "runtime");
  entry(&b, "runtime", sizeof(image) - 1, digest_img0, "app");
  entry(&b, "app", sizeof(image) - 1, digest_img0, NULL);
  run("valid again", &b, 0);

  /* A reference to an entry that is not in the manifest. */
  begin(&b, 2, "MFST", 1);
  entry(&b, "boot", sizeof(image) - 1, digest_img0, "absent");
  entry(&b, "runtime", sizeof(image) - 1, digest_img0, NULL);
  run("dangling", &b, 7);

  /* Two entries that reference each other. */
  begin(&b, 2, "MFST", 1);
  entry(&b, "one", sizeof(image) - 1, digest_img0, "two");
  entry(&b, "two", sizeof(image) - 1, digest_img0, "one");
  run("cycle", &b, 8);

  /* A header the verifier does not accept. */
  begin(&b, 1, "XXXX", 1);
  entry(&b, "boot", sizeof(image) - 1, digest_img0, NULL);
  run("bad header", &b, 2);

  /* A manifest that stops in the middle of an entry. */
  begin(&b, 2, "MFST", 1);
  put_u8(&b, 4);
  put(&b, "boot", 4);
  put_u32(&b, sizeof(image) - 1);
  b.len -= 2; /* cut the length short */
  run("truncated", &b, 1);

  /* And once more, to show the instance survived every one of them. */
  begin(&b, 3, "MFST", 1);
  entry(&b, "boot", sizeof(image) - 1, digest_img0, "runtime");
  entry(&b, "runtime", sizeof(image) - 1, digest_img0, "app");
  entry(&b, "app", sizeof(image) - 1, digest_img0, NULL);
  run("valid last", &b, 0);

  TEEC_FinalizeContext(&g_ctx);

  printf("verify_client: %s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
