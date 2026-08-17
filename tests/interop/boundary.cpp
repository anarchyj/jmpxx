// SPDX-License-Identifier: MIT
// Malformed input crossing the decode boundary yields a defined jmpxx failure,
// never an out-of-bounds read or other undefined behavior, and a well-formed input
// decodes and bridges losslessly. Run under the address and undefined-behavior
// sanitizers so a boundary defect is a finding rather than a silent corruption.
// Each check returns a distinct nonzero code.
#include "decode_boundary.hpp"

#include "jmpxx/core.hpp"
#include "jmpxx/interop/error_code.hpp"

#include <cstdio>
#include <cstring>
#include <system_error>

using namespace jmpxx;
using boundary::decode;

namespace {

// Build a well-formed buffer: domain, 4-byte little-endian code, payload length,
// then the payload.
struct buf {
  unsigned char b[64];
  std::size_t n = 0;
  void byte(unsigned char x) { b[n++] = x; }
  void code(int c) {
    auto u = static_cast<unsigned>(c);
    byte(u & 0xff);
    byte((u >> 8) & 0xff);
    byte((u >> 16) & 0xff);
    byte((u >> 24) & 0xff);
  }
};

}  // namespace

int main() {
  // 1. Empty and short buffers are truncated, not read past. A null pointer with a
  // zero size must be handled, not dereferenced.
  if (decode(nullptr, 0).error().code != boundary::truncated_header) return 1;
  {
    unsigned char five[5] = {1, 2, 3, 4, 5};
    if (decode(five, 5).error().code != boundary::truncated_header) return 2;
  }

  // 2. An out-of-range domain tag is a defined bad_domain failure.
  {
    buf x;
    x.byte(200);  // domain > 16
    x.code(7);
    x.byte(0);
    auto r = decode(x.b, x.n);
    if (r.has_value() || r.error().code != boundary::bad_domain) return 3;
  }

  // 3. A payload length that overruns the buffer is rejected, not trusted into an
  // over-read. The sanitizer would catch the resulting over-read if this check were
  // missing.
  {
    buf x;
    x.byte(1);
    x.code(7);
    x.byte(40);  // declares 40 payload bytes but supplies none
    auto r = decode(x.b, x.n);
    if (r.has_value() || r.error().code != boundary::payload_overruns) return 4;
  }

  // 4. A payload length beyond the fixed capacity is rejected even if the buffer is
  // long enough, so the fixed destination never overflows.
  {
    buf x;
    x.byte(1);
    x.code(7);
    x.byte(boundary::max_payload + 1);
    for (int i = 0; i < boundary::max_payload + 1; ++i) x.byte(0xAA);
    auto r = decode(x.b, x.n);
    if (r.has_value() || r.error().code != boundary::payload_overruns) return 5;
  }

  // 5. A well-formed buffer decodes exactly, including a negative code reassembled
  // from its bytes, and the bounded payload is copied in full.
  {
    buf x;
    x.byte(3);
    x.code(-12345);
    x.byte(4);
    x.byte('a'); x.byte('b'); x.byte('c'); x.byte('d');
    auto r = decode(x.b, x.n);
    if (!r.has_value()) return 6;
    if (r.value().domain != 3 || r.value().code != -12345) return 7;
    if (r.value().payload_len != 4 || std::memcmp(r.value().payload, "abcd", 4) != 0)
      return 8;

    // The decoded error bridges to std::error_code and back losslessly.
    error decoded(r.value().code, r.value().domain);
    std::error_code ec = to_error_code(decoded);
    if (from_error_code(ec) != decoded) return 9;
  }

  // 6. Exactly the header with a zero payload is the boundary case between truncated
  // and valid, and decodes to an empty payload.
  {
    buf x;
    x.byte(0);
    x.code(0);
    x.byte(0);
    auto r = decode(x.b, x.n);
    if (!r.has_value() || r.value().payload_len != 0) return 10;
  }

  std::printf("interop/boundary: malformed input yields defined failures, no UB\n");
  return 0;
}
