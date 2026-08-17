// SPDX-License-Identifier: MIT
// Bridge between jmpxx errors and the standard <system_error> facility,
// std::error_code. Two directions: a foreign std::error_code is carried verbatim into a
// result<T, std::error_code> (the transport is generic over its error type, so the
// category and value are preserved exactly), and a jmpxx::error is exposed as a
// std::error_code in a jmpxx-owned category that round-trips losslessly through
// to_error_code / make_error_code / from_error_code. The directions and their fidelity,
// including the lossy case of recovering a foreign category, are in
// docs/reference/interop.md.
//
// Hosted extension: <system_error> is outside the freestanding subset, so this header is
// never reached from jmpxx/core.hpp.
//
// Category identity caveat: a std::error_category is identified by the address of its
// singleton. The jmpxx categories below are held in one process-wide table, so within a
// single binary two conversions of the same error compare equal. Across a shared-library
// boundary built with hidden visibility a category can be duplicated, the same hazard
// that affects std::error_category and typeid across modules; compare on (value, domain)
// recovered through from_error_code rather than on category identity across such a
// boundary.
#ifndef JMPXX_INTEROP_ERROR_CODE_HPP
#define JMPXX_INTEROP_ERROR_CODE_HPP

#include "jmpxx/core/config.hpp"
#include "jmpxx/core/error.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>

namespace jmpxx {

namespace detail {

// One std::error_category per jmpxx domain. The domain is part of the category
// identity rather than folded into the value, so the (code, domain) pair survives
// the round-trip and the value keeps the full int range. The category renders a
// generic message because a bare jmpxx code carries no per-value text the library
// can name; a program that wants richer text uses the rich or type-erased policy.
class jmpxx_category final : public std::error_category {
 public:
  explicit jmpxx_category(int domain) noexcept : domain_(domain) {}
  [[nodiscard]] int domain() const noexcept { return domain_; }

  [[nodiscard]] const char* name() const noexcept override { return "jmpxx"; }
  [[nodiscard]] std::string message(int value) const override {
    return "jmpxx error " + std::to_string(value) + " (domain " +
           std::to_string(domain_) + ")";
  }

 private:
  int domain_;
};

// The process-wide table of jmpxx categories. by_domain hands out a stable
// category per domain so repeated conversions share identity, and by_address lets
// from_error_code recognize a category as jmpxx-owned and recover its domain. Both
// are guarded by one mutex, and the categories live for the program's lifetime so
// every error_code that names one stays valid.
struct category_table {
  std::mutex m;
  std::map<int, std::unique_ptr<jmpxx_category>> by_domain;
  std::map<const std::error_category*, int> by_address;

  const jmpxx_category& get(int domain) {
    std::lock_guard<std::mutex> lock(m);
    auto it = by_domain.find(domain);
    if (it == by_domain.end()) {
      auto inserted =
          by_domain.emplace(domain, std::make_unique<jmpxx_category>(domain));
      it = inserted.first;
      by_address.emplace(it->second.get(), domain);
    }
    return *it->second;
  }

  // The domain of a category if it is jmpxx-owned, or -1 if it is foreign. -1 is
  // not a valid table key, so it is an unambiguous "not ours".
  int domain_of(const std::error_category& cat) {
    std::lock_guard<std::mutex> lock(m);
    auto it = by_address.find(&cat);
    return it == by_address.end() ? -1 : it->second;
  }
};

inline category_table& categories() noexcept {
  static category_table table;
  return table;
}

}  // namespace detail

// The jmpxx error category for a domain, default the generic domain 0. Its identity
// is stable for the program's lifetime.
[[nodiscard]] inline const std::error_category& error_category(
    int domain = 0) noexcept {
  return detail::categories().get(domain);
}

// jmpxx::error -> std::error_code, in the jmpxx category for the error's domain.
// make_error_code is the same conversion under the name argument-dependent lookup
// finds, so a jmpxx::error flows into an error_code-typed slot without naming this
// function.
[[nodiscard]] inline std::error_code to_error_code(error e) noexcept {
  return std::error_code(e.code, error_category(e.domain));
}
[[nodiscard]] inline std::error_code make_error_code(error e) noexcept {
  return to_error_code(e);
}

// std::error_code -> jmpxx::error. What each direction preserves, and what a foreign
// category loses, is in docs/reference/interop.md. is_jmpxx is exposed beside it
// because a caller that must not narrow a foreign code needs to ask before it
// converts rather than after.
[[nodiscard]] inline bool is_jmpxx(const std::error_code& ec) noexcept {
  return detail::categories().domain_of(ec.category()) >= 0;
}
[[nodiscard]] inline error from_error_code(const std::error_code& ec) noexcept {
  int domain = detail::categories().domain_of(ec.category());
  return domain >= 0 ? error(ec.value(), domain) : error(ec.value(), 0);
}

}  // namespace jmpxx

#endif  // JMPXX_INTEROP_ERROR_CODE_HPP
