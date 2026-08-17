// SPDX-License-Identifier: MIT
// storecheck: a firmware image validator for a Cortex-M3 board with no operating system.
//
// The board holds a littlefs image in flash. On boot the firmware mounts it, walks the
// records under /records, and validates each one: a header, a schema version, a set of
// key and value pairs, and a checksum. A record may reference another record by name, and
// a reference is followed, so validation recurses to whatever depth the image contains.
//
// The recursion is why the unwind arm is here. A malformed field found eight frames down
// escapes to the single landing in check_image, and every frame between them releases the
// directory handle, the file handle, and the visit marker it owns on the way out. Those
// frames test nothing and return nothing: they call the next level and use its result. The
// portable surface would need each of them to return a result and thread it back by hand,
// which for a validator whose whole shape is recursive descent is the difference between
// the code below and one with a propagation construct on every line.
//
// The pieces are real rather than mimicked. littlefs is the filesystem a large part of
// this audience actually ships, and it is load-bearing: without it there is no image to
// validate. jmpxx is load-bearing too, and removing it removes the escape, the landing,
// and the error vocabulary in one go.
//
// The board has no heap worth the name, no operating system, and no host. What it does
// have is exception cleanup tables, which the arm requires and which the linker script
// keeps as .ARM.exidx.
#include <jmpxx/core.hpp>
#include <jmpxx/erased.hpp>
#include <jmpxx/unwind.hpp>

extern "C" {
#include "lfs.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

// What the library documents about itself, checked here by this board's compiler on this
// board's ABI. The layout gate runs on one 64-bit host, and a firmware consumer's
// question is what the types are on the target they ship. If a documented layout is
// wrong on a 32-bit freestanding target, this firmware stops building.
static_assert(sizeof(jmpxx::error) == 8, "the minimal error is documented as eight bytes");
static_assert(alignof(jmpxx::error) == 4,
              "the minimal error is documented as four-byte aligned");
static_assert(offsetof(jmpxx::error, code) == 0 && offsetof(jmpxx::error, domain) == 4,
              "the minimal error's documented field offsets do not hold here");
static_assert(sizeof(jmpxx::result<int, jmpxx::error>) == 12,
              "result<int, error> is documented as twelve bytes");
static_assert(std::is_trivially_copyable_v<jmpxx::result<int, jmpxx::error>>,
              "the transport is documented as trivially copyable over a trivial value");
static_assert(JMPXX_VERSION >= 104, "this firmware was written against jmpxx 0.1.4");

// The filesystem's own headers are in scope before jmpxx, which is the order a firmware
// consumer produces. Naming one of its macros keeps that honest: if the include order
// ever stops holding, this stops proving anything.
#ifndef LFS_NAME_MAX
#error "the filesystem's headers are not in scope; the include order this checks is gone"
#endif

namespace {

// The failure vocabulary. The code travels in the escape payload, and the offending record
// name travels beside it, because the payload is deliberately small.
enum fault {
  mount_failed = 1,
  record_unreadable,
  header_bad,
  version_unsupported,
  field_malformed,
  checksum_mismatch,
  reference_missing,
  reference_cycle,
  nesting_too_deep,
};

const char* fault_text(int f) {
  switch (f) {
    case mount_failed: return "the image did not mount";
    case record_unreadable: return "a record could not be read";
    case header_bad: return "a record header is not RCRD";
    case version_unsupported: return "a record declares an unsupported schema version";
    case field_malformed: return "a field is malformed";
    case checksum_mismatch: return "a record checksum does not match";
    case reference_missing: return "a referenced record is absent";
    case reference_cycle: return "the references form a cycle";
    case nesting_too_deep: return "the reference nesting is too deep";
  }
  return "unknown fault";
}

// The flash the filesystem lives in. A real board maps this to its flash controller; here
// it is a fixed block of memory the image is written into before the check runs, which is
// the same shape a firmware update stages.
constexpr lfs_size_t block_size = 512;
constexpr lfs_size_t block_count = 32;
alignas(8) unsigned char g_flash[block_size * block_count];

int flash_read(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buf,
               lfs_size_t size) {
  (void)c;
  std::memcpy(buf, g_flash + block * block_size + off, size);
  return 0;
}
int flash_prog(const struct lfs_config* c, lfs_block_t block, lfs_off_t off,
               const void* buf, lfs_size_t size) {
  (void)c;
  std::memcpy(g_flash + block * block_size + off, buf, size);
  return 0;
}
int flash_erase(const struct lfs_config* c, lfs_block_t block) {
  (void)c;
  std::memset(g_flash + block * block_size, 0xFF, block_size);
  return 0;
}
int flash_sync(const struct lfs_config* c) {
  (void)c;
  return 0;
}

// Static buffers, because the board has no allocator to speak of and littlefs is happy to
// be told where to work.
alignas(8) unsigned char g_read_buf[block_size];
alignas(8) unsigned char g_prog_buf[block_size];
alignas(8) unsigned char g_lookahead[16];
alignas(8) unsigned char g_file_buf[block_size];

const lfs_config g_cfg = [] {
  lfs_config c{};
  c.read = flash_read;
  c.prog = flash_prog;
  c.erase = flash_erase;
  c.sync = flash_sync;
  c.read_size = 16;
  c.prog_size = 16;
  c.block_size = block_size;
  c.block_count = block_count;
  c.block_cycles = 500;
  c.cache_size = block_size;
  c.lookahead_size = sizeof(g_lookahead);
  c.read_buffer = g_read_buf;
  c.prog_buffer = g_prog_buf;
  c.lookahead_buffer = g_lookahead;
  return c;
}();

lfs_t g_fs;

// The context an escape leaves behind. The payload carries the fault and the depth; the
// record name is larger than the payload allows, so it is written here before the escape
// and read at the landing. This is the same out-of-band pattern the diagnostic store uses.
constexpr int max_name = 32;
char g_fault_record[max_name] = {};

void note_record(const char* name) {
  std::strncpy(g_fault_record, name, max_name - 1);
  g_fault_record[max_name - 1] = 0;
}

// Escape with a fault. Not marked noreturn: the arm's escape is deliberately modeled as
// a call that may unwind, and a wrapper that claimed otherwise would let the optimizer
// remove the cleanups the escape depends on.
void fail_with(int f, const char* record, int depth) {
  note_record(record);
  jmpxx::unwind::eject(jmpxx::error(f, depth));
}

// A file handle that closes itself however the frame is left, including when an escape
// unwinds through it. Every recursion level owns one, so a deep escape releases as many
// handles as it opened, and littlefs's own handle list is left consistent.
class open_file {
 public:
  open_file(const char* path, const char* record, int depth) {
    lfs_file_config cfg{};
    cfg.buffer = g_file_buf;
    if (lfs_file_opencfg(&g_fs, &file_, path, LFS_O_RDONLY, &cfg) < 0)
      fail_with(record_unreadable, record, depth);
    open_ = true;
  }
  ~open_file() {
    if (open_) lfs_file_close(&g_fs, &file_);
  }
  open_file(const open_file&) = delete;
  open_file& operator=(const open_file&) = delete;

  lfs_ssize_t read(void* buf, lfs_size_t size) {
    return lfs_file_read(&g_fs, &file_, buf, size);
  }

 private:
  lfs_file_t file_{};
  bool open_ = false;
};

// The set of records on the current reference path, so a cycle is a fault rather than a
// stack overflow. The marker erases itself on scope exit, which is what makes the checker
// reusable after an escape: the path is empty again however the frame was left.
constexpr int max_path = 8;
const char* g_path[max_path];
int g_path_len = 0;

class visiting {
 public:
  visiting(const char* name, int depth) {
    for (int i = 0; i < g_path_len; ++i)
      if (std::strcmp(g_path[i], name) == 0) fail_with(reference_cycle, name, depth);
    if (g_path_len >= max_path) fail_with(nesting_too_deep, name, depth);
    g_path[g_path_len++] = name;
  }
  ~visiting() { --g_path_len; }
  visiting(const visiting&) = delete;
  visiting& operator=(const visiting&) = delete;
};

int g_records_checked = 0;
int g_deepest = 0;

// Validate one record, following any reference it names. Nothing here returns a failure:
// a fault escapes to the landing in check_image, and the frames between release what they
// own as the unwinder passes through them.
void check_record(const char* name, int depth) {
  if (depth > g_deepest) g_deepest = depth;
  visiting mark{name, depth};

  char path[64];
  std::snprintf(path, sizeof(path), "/records/%s", name);
  open_file file{path, name, depth};

  char buf[192] = {};
  const lfs_ssize_t n = file.read(buf, sizeof(buf) - 1);
  if (n < 12) fail_with(record_unreadable, name, depth);
  buf[n] = 0;

  if (std::strncmp(buf, "RCRD", 4) != 0) fail_with(header_bad, name, depth);
  if (buf[4] != '1') fail_with(version_unsupported, name, depth);

  // Fields are key=value lines. The sum of every value byte must equal the declared
  // checksum, and a "ref" field names another record to descend into.
  unsigned sum = 0;
  const char* ref = nullptr;
  char ref_name[max_name] = {};
  long declared = -1;
  for (const char* line = buf + 6; line && *line;) {
    const char* end = std::strchr(line, '\n');
    const char* eq = std::strchr(line, '=');
    if (!eq || (end && eq > end)) fail_with(field_malformed, name, depth);
    const char* value = eq + 1;
    const std::size_t vlen =
        end ? static_cast<std::size_t>(end - value) : std::strlen(value);
    if (std::strncmp(line, "sum=", 4) == 0) {
      declared = std::strtol(value, nullptr, 10);
    } else if (std::strncmp(line, "ref=", 4) == 0) {
      if (vlen == 0 || vlen >= max_name) fail_with(field_malformed, name, depth);
      std::memcpy(ref_name, value, vlen);
      ref_name[vlen] = 0;
      ref = ref_name;
    } else {
      for (std::size_t i = 0; i < vlen; ++i) sum += static_cast<unsigned char>(value[i]);
    }
    line = end ? end + 1 : nullptr;
  }

  if (declared < 0) fail_with(field_malformed, name, depth);
  if (static_cast<unsigned>(declared) != sum) fail_with(checksum_mismatch, name, depth);

  ++g_records_checked;

  if (ref) {
    // The oblivious recursion. No propagation construct here: a fault below escapes past
    // this frame, and the handle and the marker above are released on the way.
    struct lfs_info info;
    char ref_path[64];
    std::snprintf(ref_path, sizeof(ref_path), "/records/%s", ref);
    if (lfs_stat(&g_fs, ref_path, &info) < 0) fail_with(reference_missing, ref, depth);
    check_record(ref, depth + 1);
  }
}

// The single landing. Every fault below arrives here with its code and depth, and the
// record name is read from the context the escape left.
jmpxx::result<int, jmpxx::error> check_image(const char* entry) {
  return jmpxx::unwind::escape_scope<jmpxx::error>([entry]() -> int {
    if (lfs_mount(&g_fs, &g_cfg) < 0) fail_with(mount_failed, entry, 0);
    g_records_checked = 0;
    g_deepest = 0;
    check_record(entry, 0);
    lfs_unmount(&g_fs);
    return g_records_checked;
  });
}

// Write one record into the image.
void put_record(const char* name, const char* body) {
  char path[64];
  std::snprintf(path, sizeof(path), "/records/%s", name);
  lfs_file_t f;
  lfs_file_config cfg{};
  cfg.buffer = g_file_buf;
  if (lfs_file_opencfg(&g_fs, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &cfg) <
      0)
    return;
  lfs_file_write(&g_fs, &f, body, static_cast<lfs_size_t>(std::strlen(body)));
  lfs_file_close(&g_fs, &f);
}

// Stage an image on the board's flash. A firmware update would receive this over a link;
// here it is written directly, which exercises the same read and write paths.
void stage_image() {
  std::memset(g_flash, 0xFF, sizeof(g_flash));
  lfs_format(&g_fs, &g_cfg);
  lfs_mount(&g_fs, &g_cfg);
  lfs_mkdir(&g_fs, "/records");
  // A valid chain three deep. Each value's bytes sum to the declared checksum.
  put_record("boot", "RCRD1\nname=ab\nsum=195\nref=net\n");        // 'a'+'b' = 195
  put_record("net", "RCRD1\nhost=aa\nsum=194\nref=leaf\n");        // 'a'+'a' = 194
  put_record("leaf", "RCRD1\nvalue=abc\nsum=294\n");               // 'a'+'b'+'c' = 294
  // A chain whose deepest record has a wrong checksum.
  put_record("bad_boot", "RCRD1\nname=ab\nsum=195\nref=bad_leaf\n");
  put_record("bad_leaf", "RCRD1\nvalue=abc\nsum=1\n");
  // A chain that refers to a record that is not there.
  put_record("dangling", "RCRD1\nname=ab\nsum=195\nref=absent\n");
  // A cycle.
  put_record("loop_a", "RCRD1\nname=ab\nsum=195\nref=loop_b\n");
  put_record("loop_b", "RCRD1\nname=ab\nsum=195\nref=loop_a\n");
  // A record whose header is wrong.
  put_record("corrupt", "XXXX1\nvalue=abc\nsum=294\n");
  lfs_unmount(&g_fs);
}

int g_failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("  FAIL: %s\n", what);
    ++g_failures;
  }
}

void run(const char* entry, int expected_fault, const char* expected_record) {
  g_fault_record[0] = 0;
  const auto r = check_image(entry);
  if (expected_fault == 0) {
    std::printf("  %-9s ok, %d record(s) checked, deepest reference %d\n", entry,
                r.value_or(-1), g_deepest);
    expect(r.has_value(), "a valid image validates");
    expect(g_path_len == 0, "the reference path is empty after a valid image");
    return;
  }
  std::printf("  %-9s fault %d at depth %d in '%s': %s\n", entry,
              r.has_value() ? 0 : r.error().code, r.has_value() ? -1 : r.error().domain,
              g_fault_record, fault_text(r.has_value() ? 0 : r.error().code));
  expect(!r.has_value(), "a bad image reports a fault");
  expect(!r.has_value() && r.error().code == expected_fault, "the fault is the expected one");
  expect(std::strcmp(g_fault_record, expected_record) == 0,
         "the offending record is named");
  // The property the arm exists for: every frame the escape passed through released what
  // it owned, so the checker is immediately reusable.
  expect(g_path_len == 0, "the reference path is empty after an escape");
}

}  // namespace

int main() {
  std::printf("storecheck: jmpxx %s, unwind arm available=%d\n", JMPXX_VERSION_STRING,
              static_cast<int>(jmpxx::unwind::available()));
  // The board reports the layout it observes, so the figures the documentation states
  // are readable from a run on the target rather than only from a host measurement.
  std::printf("storecheck: on this board sizeof(error)=%u result<int,error>=%u "
              "erased_error=%u alignof(error)=%u\n",
              static_cast<unsigned>(sizeof(jmpxx::error)),
              static_cast<unsigned>(sizeof(jmpxx::result<int, jmpxx::error>)),
              static_cast<unsigned>(sizeof(jmpxx::erased_error)),
              static_cast<unsigned>(alignof(jmpxx::error)));
  stage_image();

  run("boot", 0, "");
  run("bad_boot", checksum_mismatch, "bad_leaf");
  run("dangling", reference_missing, "absent");
  run("loop_a", reference_cycle, "loop_a");
  run("corrupt", header_bad, "corrupt");
  run("absent", record_unreadable, "absent");

  // Every fault above ran on the same mounted filesystem in sequence. That only works
  // because each escape released the handles and markers its frames held, which is the
  // guarantee a bare longjmp would break.
  run("boot", 0, "");

  std::printf("storecheck: %s\n", g_failures == 0 ? "PASS" : "FAIL");
  return g_failures == 0 ? 0 : 1;
}
