// SPDX-License-Identifier: MIT
// A component that loads an access policy expressed as JSON, embedded in the TOML
// configuration, using glaze. glaze is a third-party JSON library whose read API
// returns std::expected, so it exercises the expected bridge against a real external
// library: the std::expected glaze returns is converted into a jmpxx result with the
// expected bridge, losslessly and carrying glaze's own error type, so the policy
// component reports failures with glaze's error type while still propagating them
// through jmpxx.
//
// This mirrors the [limits] component's use of the type-erased policy: a component
// with its own error representation read at a boundary, except here the error type
// is a third-party one adopted through the interop bridge rather than jmpxx's own.
// The program reads three error representations at three boundaries, the minimal or
// rich app error for [server], the type-erased error for [limits], and glaze's error
// for [policy], over one transport and one propagation construct.
#ifndef JMPXX_APP_POLICY_CHECK_HPP
#define JMPXX_APP_POLICY_CHECK_HPP

#include <jmpxx/core.hpp>
#include <jmpxx/interop/expected.hpp>

#include <glaze/glaze.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace policy {

// The access policy parsed from JSON. glaze reflects an aggregate struct directly,
// so no hand-written schema is needed; the field names match the JSON keys.
struct access {
  std::string mode;
  int max_sessions = 0;
  std::vector<std::string> allow;
};

// Parse the JSON policy and adopt the result into jmpxx. glz::read_json returns a
// std::expected<access, glz::error_ctx>; from_expected converts it to a jmpxx result
// over the same error type without loss, so a malformed policy yields a defined
// failure carrying glaze's parse diagnostic rather than a thrown exception. glaze
// runs with exceptions disabled, which is the configuration the application builds in.
[[nodiscard]] inline jmpxx::result<access, glz::error_ctx> parse(
    std::string_view json) {
  return jmpxx::from_expected(glz::read_json<access>(json));
}

}  // namespace policy

#endif  // JMPXX_APP_POLICY_CHECK_HPP
