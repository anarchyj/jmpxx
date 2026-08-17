// SPDX-License-Identifier: MIT
// Adopting jmpxx at a boundary in a codebase that already speaks the standard error
// vocabularies: one function per bridge, each converting in and back out so the
// round trip is visible in the output.
#include <jmpxx/core.hpp>
#include <jmpxx/interop/adapt.hpp>
#include <jmpxx/interop/error_code.hpp>
#include <jmpxx/interop/expected.hpp>

#include <cstdio>
#include <optional>
#include <string_view>
#include <system_error>

using jmpxx::error;
using jmpxx::fail;
using jmpxx::result;

int main() {
  // std::expected, both directions, lossless.
#if JMPXX_INTEROP_HAS_EXPECTED
  result<int, error> r = fail(error(9, 3));
  std::expected<int, error> e = jmpxx::to_expected(r);
  result<int, error> back = jmpxx::from_expected(e);
  std::printf("expected round-trip preserved: %d\n",
              back.has_error() && back.error() == error(9, 3));
#endif

  // A jmpxx error exposed as a std::error_code and recovered exactly.
  error original{5, 2};
  std::error_code ec = jmpxx::to_error_code(original);
  std::printf("error_code round-trip preserved: %d (category=%s)\n",
              jmpxx::from_error_code(ec) == original, ec.category().name());

  // A foreign error_code carried verbatim, because the transport is generic over E.
  std::error_code foreign = std::make_error_code(std::errc::timed_out);
  result<int, std::error_code> carried = fail(foreign);
  std::printf("foreign error_code carried verbatim: %d\n",
              carried.error() == foreign);

  // from_optional turns an absent std::optional into a failure in one call.
  std::optional<int> present = 42, absent;
  auto a = jmpxx::from_optional<error>(present, [] { return error(1); });
  auto b = jmpxx::from_optional<error>(absent, [] { return error(2); });
  std::printf("from_optional present=%d absent_failed=%d\n", a.value(),
              b.has_error());
  return 0;
}
