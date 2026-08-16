// SPDX-License-Identifier: MIT
// A manifest verifier that runs in the ARM secure world as an OP-TEE trusted application.
//
// The normal world hands in a manifest describing a chain of firmware images: each entry
// names an image, declares its length and its SHA-256 digest, and may reference a further
// manifest that must verify before it does. This is the shape a secure boot stage has, and
// verification here is recursive descent over an untrusted buffer.
//
// The unwind arm is the error spine. A fault found six levels down, a truncated entry, a
// digest that does not match, a reference that loops, escapes to the single landing in
// verify_manifest, and every level between releases the hash context and the visit marker
// it owns as the unwinder passes through. The levels themselves carry no propagation
// construct: each one parses its entry and descends, which is what the recursion would
// look like if it could not fail at all.
//
// Two properties matter more here than in an ordinary program. The input is untrusted, so
// every read is bounded against the buffer the client supplied and a malformed manifest is
// a fault rather than a read past the end. And the instance is kept alive across
// invocations, so the state a failed verification leaves behind is the state the next one
// starts from: the visit path must be empty and every hash context released, or the second
// call misbehaves. That is exactly the guarantee a bare longjmp would not give, and it is
// checked from the normal world by verifying a bad manifest and then a good one.
//
// The trusted application is compiled with exception cleanup tables, which OP-TEE's
// trusted-application development kit provides for C++ by linking libgcc's unwinder with
// an exception-frame header. Without them the arm would skip destructors, so the arm's
// precondition and this build configuration are the same requirement.
// Include order here is load-bearing, and each step is a constraint the environment
// imposes rather than a preference.
//
// setjmp.h comes first and inside extern "C", because the development kit's copy carries
// no linkage guard of its own and the arm's <csetjmp> only re-exports whatever linkage it
// already declared. Without this the landing's setjmp resolves to a C++-mangled name that
// nothing defines.
//
// The library and the C++ headers it uses come next, before the kit's headers, because
// those headers define short lowercase-underscore macros that collide with libstdc++'s
// internal member names: __data among them, which makes <type_traits> fail to parse if it
// is read afterwards.
//
// The kit's headers come last, in extern "C" for the same reason as setjmp.h: they are C
// and the entry points below have to match the C symbols the generated header expects.
extern "C" {
#include <setjmp.h>
}

// setjmp.h reaches the kit's compiler.h, which defines __data as a section attribute.
// libstdc++ uses __data as a member name inside <type_traits>, so the macro has to stand
// aside while the C++ headers are read and be restored for the kit's own headers below.
// This is header pollution in the environment rather than anything the library does, and
// it is confined to these four lines.
#pragma push_macro("__data")
#undef __data

#include <jmpxx/core.hpp>
#include <jmpxx/unwind.hpp>

#pragma pop_macro("__data")

extern "C" {
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
}

#include "manifest_ta.h"

namespace {

// The faults a verification can report. The code travels in the escape payload with the
// depth; the offending image name travels beside it, because the payload is small on
// purpose and a name is not.
enum fault {
  ok = 0,
  manifest_truncated = 1,
  header_bad,
  version_unsupported,
  entry_malformed,
  length_implausible,
  digest_mismatch,
  reference_missing,
  reference_cycle,
  nesting_too_deep,
  digest_unavailable,
};

constexpr int max_depth = 6;
constexpr int max_name = 24;
constexpr uint32_t max_image_bytes = 1u << 20;
constexpr size_t digest_bytes = 32;

// The manifest as the normal world laid it out. Every field is read through the reader
// below rather than by casting the buffer, so a truncated or hostile manifest cannot make
// this read past its end.
//
//   "MFST" version(1) count(1)
//   repeated count times:
//     name_len(1) name[name_len] length(4, little endian) digest[32]
//     ref_len(1) ref[ref_len] image[length]
//
// A ref names another entry in the same manifest that must verify first. Each entry
// carries its own image inline, so an entry is verifiable wherever it sits in the
// manifest rather than only when it happens to be the last one.
class reader {
 public:
  reader(const uint8_t* base, size_t size) : base_(base), size_(size) {}

  [[nodiscard]] bool has(size_t n) const noexcept { return pos_ + n <= size_; }
  [[nodiscard]] size_t offset() const noexcept { return pos_; }
  void seek(size_t p) noexcept { pos_ = p; }

  [[nodiscard]] uint8_t u8() noexcept { return base_[pos_++]; }
  [[nodiscard]] uint32_t u32() noexcept {
    const uint32_t v = static_cast<uint32_t>(base_[pos_]) |
                       (static_cast<uint32_t>(base_[pos_ + 1]) << 8) |
                       (static_cast<uint32_t>(base_[pos_ + 2]) << 16) |
                       (static_cast<uint32_t>(base_[pos_ + 3]) << 24);
    pos_ += 4;
    return v;
  }
  [[nodiscard]] const uint8_t* take(size_t n) noexcept {
    const uint8_t* p = base_ + pos_;
    pos_ += n;
    return p;
  }

 private:
  const uint8_t* base_;
  size_t size_;
  size_t pos_ = 0;
};

// What an escape leaves behind for the landing to report. It is reset at the start of each
// verification, so a stale name from a previous call cannot be read as this one's.
char g_fault_name[max_name + 1];
int g_entries_verified;
int g_deepest;

void note_name(const uint8_t* name, size_t len) {
  const size_t n = len < max_name ? len : max_name;
  for (size_t i = 0; i < n; ++i) g_fault_name[i] = static_cast<char>(name[i]);
  g_fault_name[n] = 0;
}

// Escape with a fault. Deliberately not marked noreturn: the arm models its escape as a
// call that may unwind, and a wrapper claiming otherwise would let the optimizer delete
// the cleanups the escape depends on.
void fail(int f, const uint8_t* name, size_t name_len, int depth) {
  note_name(name, name_len);
  jmpxx::unwind::eject(jmpxx::error(f, depth));
}

// A hash context that frees itself however its frame is left, including when an escape
// unwinds through it. Each level of the descent owns one, so a deep escape releases as
// many contexts as it allocated and the trusted application's handle table is left clean
// for the next invocation. Leaking one here would exhaust a long-lived secure-world
// instance, which is the failure this type exists to prevent.
class digest {
 public:
  explicit digest(int depth) {
    if (TEE_AllocateOperation(&op_, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0) != TEE_SUCCESS) {
      static const uint8_t none[] = "digest";
      fail(digest_unavailable, none, sizeof(none) - 1, depth);
    }
  }
  ~digest() {
    if (op_ != TEE_HANDLE_NULL) TEE_FreeOperation(op_);
  }
  digest(const digest&) = delete;
  digest& operator=(const digest&) = delete;

  void update(const void* data, size_t len) { TEE_DigestUpdate(op_, data, len); }
  void finish(uint8_t out[digest_bytes]) {
    size_t len = digest_bytes;
    TEE_DigestDoFinal(op_, nullptr, 0, out, &len);
  }

 private:
  TEE_OperationHandle op_ = TEE_HANDLE_NULL;
};

// The entries on the current reference path, so a cycle is a fault rather than a stack
// overflow in the secure world. The marker erases itself on scope exit, which is what
// leaves the path empty after an escape and makes the instance reusable.
struct path_entry {
  const uint8_t* name;
  size_t len;
};
path_entry g_path[max_depth + 1];
int g_path_len;

bool same_name(const path_entry& a, const uint8_t* name, size_t len) {
  if (a.len != len) return false;
  for (size_t i = 0; i < len; ++i)
    if (a.name[i] != name[i]) return false;
  return true;
}

class visiting {
 public:
  visiting(const uint8_t* name, size_t len, int depth) {
    for (int i = 0; i < g_path_len; ++i)
      if (same_name(g_path[i], name, len)) fail(reference_cycle, name, len, depth);
    if (g_path_len > max_depth) fail(nesting_too_deep, name, len, depth);
    g_path[g_path_len++] = {name, len};
  }
  ~visiting() { --g_path_len; }
  visiting(const visiting&) = delete;
  visiting& operator=(const visiting&) = delete;
};

// One parsed entry, located by name. Returns the offset its body starts at, or reports
// that the name is absent.
struct located {
  size_t offset;
  bool found;
};

located find_entry(const uint8_t* buf, size_t size, const uint8_t* want, size_t want_len) {
  reader r{buf, size};
  if (!r.has(6)) return {0, false};
  (void)r.take(4);
  (void)r.u8();
  const uint8_t count = r.u8();
  for (uint8_t i = 0; i < count; ++i) {
    if (!r.has(1)) return {0, false};
    const uint8_t name_len = r.u8();
    if (!r.has(name_len)) return {0, false};
    const uint8_t* name = r.take(name_len);
    const size_t body = r.offset();
    if (!r.has(4 + digest_bytes + 1)) return {0, false};
    const uint32_t length = r.u32();
    (void)r.take(digest_bytes);
    const uint8_t ref_len = r.u8();
    if (!r.has(ref_len)) return {0, false};
    (void)r.take(ref_len);
    if (!r.has(length)) return {0, false};
    (void)r.take(length);
    if (name_len == want_len) {
      bool match = true;
      for (size_t k = 0; k < want_len; ++k)
        if (name[k] != want[k]) { match = false; break; }
      if (match) return {body, true};
    }
  }
  return {0, false};
}

// Verify one entry and, if it references another, descend into that one first. Nothing
// here returns a failure: a fault escapes to the landing, and this frame's hash context
// and visit marker are released as the unwinder passes through.
void verify_entry(const uint8_t* buf, size_t size, const uint8_t* name, size_t name_len,
                  size_t body, int depth) {
  if (depth > g_deepest) g_deepest = depth;
  visiting mark{name, name_len, depth};
  digest hash{depth};

  reader r{buf, size};
  r.seek(body);
  if (!r.has(4 + digest_bytes + 1)) fail(manifest_truncated, name, name_len, depth);
  const uint32_t length = r.u32();
  const uint8_t* expected = r.take(digest_bytes);
  const uint8_t ref_len = r.u8();
  if (!r.has(ref_len)) fail(manifest_truncated, name, name_len, depth);
  const uint8_t* ref = r.take(ref_len);

  if (length == 0 || length > max_image_bytes)
    fail(length_implausible, name, name_len, depth);

  if (ref_len != 0) {
    // The oblivious recursion: a fault below escapes past this frame, and the hash context
    // above is freed on the way out without this frame testing anything.
    const located target = find_entry(buf, size, ref, ref_len);
    if (!target.found) fail(reference_missing, ref, ref_len, depth);
    verify_entry(buf, size, ref, ref_len, target.offset, depth + 1);
  }

  // The image bytes follow this entry's reference. A real boot stage reads them from
  // flash; here the client inlines them, which exercises the same bounded read.
  if (!r.has(length)) fail(manifest_truncated, name, name_len, depth);
  const uint8_t* image = r.take(length);
  hash.update(image, length);

  uint8_t actual[digest_bytes];
  hash.finish(actual);
  for (size_t i = 0; i < digest_bytes; ++i)
    if (actual[i] != expected[i]) fail(digest_mismatch, name, name_len, depth);

  ++g_entries_verified;
}

// The single landing. Every fault below arrives here with its code and depth.
jmpxx::result<int, jmpxx::error> verify_manifest(const uint8_t* buf, size_t size) {
  g_fault_name[0] = 0;
  g_entries_verified = 0;
  g_deepest = 0;
  return jmpxx::unwind::escape_scope<jmpxx::error>([buf, size]() -> int {
    reader r{buf, size};
    static const uint8_t root[] = "root";
    if (!r.has(6)) fail(manifest_truncated, root, 4, 0);
    const uint8_t* magic = r.take(4);
    if (magic[0] != 'M' || magic[1] != 'F' || magic[2] != 'S' || magic[3] != 'T')
      fail(header_bad, root, 4, 0);
    if (r.u8() != 1) fail(version_unsupported, root, 4, 0);
    const uint8_t count = r.u8();
    if (count == 0) fail(entry_malformed, root, 4, 0);

    if (!r.has(1)) fail(manifest_truncated, root, 4, 0);
    const uint8_t first_len = r.u8();
    if (first_len == 0 || !r.has(first_len)) fail(entry_malformed, root, 4, 0);
    const uint8_t* first = r.take(first_len);
    verify_entry(buf, size, first, first_len, r.offset(), 0);
    return g_entries_verified;
  });
}

}  // namespace

TEE_Result TA_CreateEntryPoint(void) {
  g_path_len = 0;
  return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {}

TEE_Result TA_OpenSessionEntryPoint(uint32_t, TEE_Param[4], void**) {
  return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void*) {}

TEE_Result TA_InvokeCommandEntryPoint(void*, uint32_t cmd, uint32_t types,
                                      TEE_Param params[4]) {
  if (cmd != TA_MANIFEST_CMD_VERIFY) return TEE_ERROR_NOT_SUPPORTED;
  const uint32_t expected = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                            TEE_PARAM_TYPE_VALUE_OUTPUT,
                                            TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                            TEE_PARAM_TYPE_NONE);
  if (types != expected) return TEE_ERROR_BAD_PARAMETERS;

  const auto* buf = static_cast<const uint8_t*>(params[0].memref.buffer);
  const size_t size = params[0].memref.size;
  if (!buf || size == 0) return TEE_ERROR_BAD_PARAMETERS;

  const auto verdict = verify_manifest(buf, size);

  // The client learns the fault and the depth, and reads the offending name from the
  // third parameter. The path being empty is reported too, because that is the property
  // the arm provides and the client checks across repeated invocations.
  params[1].value.a = verdict.has_value() ? static_cast<uint32_t>(ok)
                                          : static_cast<uint32_t>(verdict.error().code);
  params[1].value.b = verdict.has_value()
                          ? static_cast<uint32_t>(g_entries_verified)
                          : static_cast<uint32_t>(verdict.error().domain);

  size_t n = 0;
  while (g_fault_name[n] && n < max_name) ++n;
  if (params[2].memref.size < n + 2) return TEE_ERROR_SHORT_BUFFER;
  auto* out = static_cast<uint8_t*>(params[2].memref.buffer);
  for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(g_fault_name[i]);
  out[n] = 0;
  // A one-byte tail the client reads as the reuse check: zero means the reference path was
  // left empty, which is what makes the next invocation correct.
  out[n + 1] = static_cast<uint8_t>(g_path_len);
  params[2].memref.size = static_cast<uint32_t>(n + 2);
  return TEE_SUCCESS;
}
