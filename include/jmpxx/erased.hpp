// SPDX-License-Identifier: MIT
// The type-erased boundary policy.
//
// erased_error carries a domain-tagged error that code on the far side of a
// component boundary can inspect without knowing the originating error category;
// docs/reference/policies.md states what that boundary reads and what identity it
// can rely on. The representation is two scalars and a static descriptor because a
// boundary error must still travel in registers and allocate nothing, and the
// descriptor's virtual dispatch is what carries type identity without RTTI, since
// only typeid and dynamic_cast need it.
//
// Hosted extension. The minimal core never pulls it in, but the header follows the
// core's freestanding-friendly discipline and includes no hosted header, so a
// freestanding boundary can still erase errors when it selects this policy explicitly.
#ifndef JMPXX_ERASED_HPP
#define JMPXX_ERASED_HPP

// The full minimal core, so the type-erased policy is usable from this header alone:
// result and the transport the policy is selected over, plus the minimal error. The
// core is freestanding, so pulling it in keeps this header freestanding-friendly.
#include "jmpxx/core.hpp"

namespace jmpxx {

// A descriptor for one family of error values: how the family is named and how a
// value within it renders to text. One static instance exists per family, and an
// erased_error points at it, so the descriptor's address is the family's identity.
//
// Concurrency: a domain is immutable after construction and its methods are
// const, so a single static instance is shared across threads without
// synchronization. Lifetime: domains have static storage duration and are never
// destroyed through this base, so the protected destructor forbids
// `delete base_ptr`. message() returns a string with static storage duration so
// it allocates nothing and the caller need not free it.
class error_domain {
 public:
  constexpr error_domain() noexcept = default;
  error_domain(const error_domain&) = delete;
  error_domain& operator=(const error_domain&) = delete;

  // A short, stable identifier for the family, for example "jmpxx.generic".
  [[nodiscard]] virtual const char* name() const noexcept = 0;

  // A human-readable rendering of one value in this family, with static storage
  // duration. A domain that has no per-value text returns a family-level string.
  [[nodiscard]] virtual const char* message(int value) const noexcept = 0;

 protected:
  ~error_domain() = default;
};

namespace detail {

[[nodiscard]] constexpr int fold_generic_error(int code, int domain_tag) noexcept {
  return static_cast<int>(static_cast<unsigned>(code) ^
                          (static_cast<unsigned>(domain_tag) << 16));
}

// The built-in domain for errors that carry only a bare code, so the same
// call-site source that constructs a minimal error constructs an erased one. It
// names itself and renders every value with one family-level string, because a
// bare code carries no per-value meaning the library can name.
class generic_error_domain final : public error_domain {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "jmpxx.generic"; }
  [[nodiscard]] const char* message(int) const noexcept override {
    return "unspecified error";
  }
};

inline constexpr generic_error_domain generic_domain_instance{};

}  // namespace detail

// The generic domain singleton. Its address is a constant expression, so an
// erased_error built from a bare code is usable in constant evaluation.
[[nodiscard]] constexpr const error_domain& generic_domain() noexcept {
  return detail::generic_domain_instance;
}

// A type-erased, domain-tagged error; the boundary contract is in
// docs/reference/policies.md. The value is held beside the descriptor rather than
// inside it so that two errors from one domain differ without a second descriptor.
class erased_error {
  int value_ = 0;
  const error_domain* domain_ = &generic_domain();

  [[nodiscard]] constexpr const error_domain* checked_domain() const noexcept {
    if constexpr (JMPXX_HARDENING_EXTENSIVE_ENABLED) {
      if (JMPXX_UNLIKELY(domain_ == nullptr))
        platform::fail_fast("jmpxx::erased_error: null error domain");
    }
    return domain_;
  }

 public:
  constexpr erased_error() noexcept = default;

  // Policy-uniform construction from a bare code, matching the minimal error's
  // shape so identical call-site source serves both policies. The second argument
  // is the minimal error's coarse domain tag, folded into the value by unsigned
  // arithmetic over the low bits of the two integers. The fold round-trips a code
  // and a tag that each fit in 16 bits, which covers the common small-code case at
  // no extra storage, but it is coarse, not lossless in general: a code that uses
  // the high 16 bits can collide with a tagged code, for example erased_error(0, 1)
  // and erased_error(65536, 0) both hold 65536. Code that must carry a full-width
  // code and a distinct domain across a boundary names a domain descriptor through
  // the (value, error_domain&) constructor below instead.
  constexpr explicit erased_error(int code, int domain_tag = 0) noexcept
      : value_(detail::fold_generic_error(code, domain_tag)),
        domain_(&generic_domain()) {}

  // Boundary construction: a component erases its own error into the uniform form
  // by naming its domain. The value is the component's own code within that
  // domain.
  constexpr erased_error(int value, const error_domain& dom) noexcept
      : value_(value), domain_(&dom) {}

  // Erase a minimal error into a named domain, preserving its code as the value.
  constexpr erased_error(error e, const error_domain& dom) noexcept
      : value_(e.code), domain_(&dom) {}

  [[nodiscard]] constexpr int value() const noexcept { return value_; }
  [[nodiscard]] constexpr const error_domain& domain() const noexcept {
    return *checked_domain();
  }

  // Same-domain membership by descriptor identity, without RTTI.
  [[nodiscard]] constexpr bool in_domain(const error_domain& dom) const noexcept {
    return checked_domain() == &dom;
  }

  [[nodiscard]] const char* domain_name() const noexcept {
    return checked_domain()->name();
  }
  [[nodiscard]] const char* message() const noexcept {
    return checked_domain()->message(value_);
  }

  // Two erased errors are equal when they name the same domain and the same value.
  [[nodiscard]] friend constexpr bool operator==(erased_error a,
                                                  erased_error b) noexcept {
    return a.domain_ == b.domain_ && a.value_ == b.value_;
  }
};

static_assert(__is_trivially_copyable(erased_error),
              "erased_error must stay trivially copyable so the transport that "
              "carries it stays register-passable and allocation-free");

}  // namespace jmpxx

#endif  // JMPXX_ERASED_HPP
