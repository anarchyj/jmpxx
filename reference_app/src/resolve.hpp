// SPDX-License-Identifier: MIT
// A recursive configuration reference resolver that opts into the experimental
// non-local unwind arm.
//
// A [values] table may interpolate other values with ${other}; resolving one value
// recurses into each reference it embeds and substitutes the result. The recursion is
// where the arm is used. A failure found deep in the recursion, a reference cycle, a
// missing reference, malformed syntax, or runaway nesting, escapes to the single landing
// at the top, and the recursion's intermediate frames neither test nor thread the error:
// they call resolve and use its result. Their cycle-detection guards
// still run as the escape unwinds, so the resolver's "currently resolving" set is
// restored and the resolver is reusable afterward. That RAII-correctness across a
// non-local escape is the property the arm provides and a bare longjmp would break.
//
// Opt-in surface: the translation unit that includes it is built with exception cleanup
// tables, unlike the portable config_validate program. That cost is the arm's, and the
// reference documents it.
#ifndef JMPXX_APP_RESOLVE_HPP
#define JMPXX_APP_RESOLVE_HPP

#include <jmpxx/core.hpp>
#include <jmpxx/unwind.hpp>

#include <toml++/toml.hpp>

#include <string>
#include <string_view>
#include <unordered_set>

namespace resolve {

// The failure kinds a resolution can hit, carried in the in-band error code. The
// offending key and the recursion depth travel beside the small escape payload so the
// landing can report them without widening it.
enum kind { missing = 1, cycle = 2, too_deep = 3, malformed = 4 };

inline constexpr int max_depth = 64;

[[nodiscard]] inline const char* kind_text(int k) noexcept {
  switch (k) {
    case missing: return "a referenced value is missing";
    case cycle: return "a reference cycle was found";
    case too_deep: return "the reference nesting is too deep";
    case malformed: return "a ${...} reference is malformed";
  }
  return "unknown failure";
}

class resolver {
 public:
  explicit resolver(const toml::table& values) : values_(values) {}

  // Resolve one key to its fully interpolated value, or report the first failure. The
  // landing boundary is here; the recursion below it is oblivious to the escape.
  jmpxx::result<std::string, jmpxx::error> resolve_key(std::string_view key) {
    return jmpxx::unwind::escape_scope<jmpxx::error>(
        [&] { return resolve(std::string(key), 0); });
  }

  // After a failed resolve_key, the offending key read from the context the escape left
  // behind. The kind is the error code the result already carries.
  [[nodiscard]] const std::string& error_key() const noexcept { return error_key_; }

 private:
  // Resolve `key` at recursion `depth`, ejecting on the first failure. Callers need no
  // propagation construct: this ejects rather than returning a failure to thread back.
  std::string resolve(const std::string& key, int depth) {
    if (depth > max_depth) eject_with(too_deep, key, depth);

    // A key already on the resolution path is a cycle. The guard erases the key on
    // scope exit, including when an escape unwinds this frame, so the set is restored
    // however the function leaves.
    if (resolving_.count(key)) eject_with(cycle, key, depth);
    struct mark {
      std::unordered_set<std::string>& set;
      const std::string& k;
      ~mark() { set.erase(k); }
    } guard{resolving_, key};
    resolving_.insert(key);
    (void)guard;

    auto raw = values_[key].value<std::string>();
    if (!raw) eject_with(missing, key, depth);

    return interpolate(*raw, depth);
  }

  // Replace every ${ref} in `text` with the resolved value of ref, recursing into each.
  std::string interpolate(const std::string& text, int depth) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
      std::size_t open = text.find("${", i);
      if (open == std::string::npos) {
        out.append(text, i, std::string::npos);
        break;
      }
      out.append(text, i, open - i);
      std::size_t close = text.find('}', open + 2);
      if (close == std::string::npos) eject_with(malformed, text, depth);
      std::string ref = text.substr(open + 2, close - (open + 2));
      out += resolve(ref, depth + 1);  // oblivious recursion: a deep failure ejects
      i = close + 1;
    }
    return out;
  }

  // Record the offending key out of band and eject the kind and depth. Left as an
  // ordinary function so a caller cannot prove it never unwinds, which is the proof that
  // would delete the cleanups the escape depends on; it does not return at run time.
  void eject_with(int k, const std::string& key, int depth) {
    error_key_ = key;
    jmpxx::unwind::eject(jmpxx::error(k, depth));
  }

  const toml::table& values_;
  std::unordered_set<std::string> resolving_;
  std::string error_key_;
};

}  // namespace resolve

#endif  // JMPXX_APP_RESOLVE_HPP
