// SPDX-License-Identifier: MIT
// result<T, E>: the value-or-error transport.
//
// It holds exactly one of a value or an error and never presents a third,
// valueless state. When T and E are trivially copyable the transport is too, so
// it is returned in registers and copied by memcpy; this is the property that
// makes the happy path compile to a plain branch on a status flag. Construction,
// inspection, and extraction work in constant evaluation.
//
// The union, the discriminant, and the special members live directly in this
// class rather than in a base. A base subobject defeats GCC's return-value
// scalar replacement, which would force the transport onto the stack at a
// non-inlined boundary instead of into a register; keeping everything in one
// class preserves register return.
//
// Two access forms are offered. The checked accessors (value, error, operator*,
// operator->) verify the state and terminate on a violation, so reading a value
// from a failed result is a defined event rather than undefined behavior. The
// narrow accessors (assume_value, assume_error) state a precondition that the
// caller has already checked the state; that precondition is verified only in a
// hardened build and is otherwise the caller's contract.
#ifndef JMPXX_CORE_TRANSPORT_HPP
#define JMPXX_CORE_TRANSPORT_HPP

#include "jmpxx/core/config.hpp"
#include "jmpxx/core/detail/utility.hpp"
#include "jmpxx/core/error.hpp"
#include "jmpxx/platform/trap.hpp"

#include <new>
#include <type_traits>

namespace jmpxx {

namespace detail {
// Empty stand-in for the value of a result<void, E>.
struct unit {};
}  // namespace detail

// failure<E> wraps an error so it can be returned or assigned without being
// mistaken for a value, including when the value and error types are the same.
// It converts into any result whose error type can be built from E.
template <class E>
class failure {
  E err_;

 public:
  constexpr explicit failure(const E& e) : err_(e) {}
  constexpr explicit failure(E&& e) : err_(static_cast<E&&>(e)) {}

  constexpr E& error() & noexcept { return err_; }
  constexpr const E& error() const& noexcept { return err_; }
  constexpr E&& error() && noexcept { return static_cast<E&&>(err_); }
  constexpr const E&& error() const&& noexcept {
    return static_cast<const E&&>(err_);
  }
};

template <class E>
failure(E) -> failure<E>;

// fail(e): build a failure for `return jmpxx::fail(e);`.
template <class E>
[[nodiscard]] constexpr failure<std::remove_cvref_t<E>> fail(E&& e) {
  return failure<std::remove_cvref_t<E>>(static_cast<E&&>(e));
}

struct in_place_t {
  explicit in_place_t() = default;
};
inline constexpr in_place_t in_place{};

namespace detail {
template <class>
inline constexpr bool is_failure_v = false;
template <class E>
inline constexpr bool is_failure_v<failure<E>> = true;
}  // namespace detail

template <class T, class E = error>
class JMPXX_NODISCARD(
    "a result may hold a failure; inspect it before discarding") result {
  using V = std::conditional_t<std::is_void_v<T>, detail::unit, T>;
  static constexpr bool is_void = std::is_void_v<T>;

  union {
    V val_;
    E err_;
  };
  bool has_;

  static constexpr bool triv_copy_ctor =
      std::is_trivially_copy_constructible_v<V> &&
      std::is_trivially_copy_constructible_v<E>;
  static constexpr bool triv_move_ctor =
      std::is_trivially_move_constructible_v<V> &&
      std::is_trivially_move_constructible_v<E>;
  static constexpr bool triv_dtor =
      std::is_trivially_destructible_v<V> && std::is_trivially_destructible_v<E>;
  static constexpr bool triv_copy_assign =
      triv_copy_ctor && triv_dtor && std::is_trivially_copy_assignable_v<V> &&
      std::is_trivially_copy_assignable_v<E>;
  static constexpr bool triv_move_assign =
      triv_move_ctor && triv_dtor && std::is_trivially_move_assignable_v<V> &&
      std::is_trivially_move_assignable_v<E>;
  static constexpr bool can_copy_ctor =
      std::is_copy_constructible_v<V> && std::is_copy_constructible_v<E>;
  static constexpr bool can_move_ctor =
      std::is_move_constructible_v<V> && std::is_move_constructible_v<E>;
  // Cross-state assignment reconstructs the incoming alternative in place after
  // destroying the outgoing one, so that reconstruction must not throw; both
  // alternatives are therefore required to be nothrow-move-constructible.
  static constexpr bool can_copy_assign =
      can_copy_ctor && std::is_copy_assignable_v<V> &&
      std::is_copy_assignable_v<E> && std::is_nothrow_move_constructible_v<V> &&
      std::is_nothrow_move_constructible_v<E>;
  static constexpr bool can_move_assign =
      can_move_ctor && std::is_move_assignable_v<V> &&
      std::is_move_assignable_v<E> && std::is_nothrow_move_constructible_v<V> &&
      std::is_nothrow_move_constructible_v<E>;

 public:
  using value_type = T;
  using error_type = E;

  // Value construction (non-void): from anything convertible to T, except a
  // result, a failure, or the in_place tag, which route to their own ctors.
  template <class U = T>
    requires(!is_void && std::is_constructible_v<V, U> &&
             !std::is_same_v<std::remove_cvref_t<U>, result> &&
             !std::is_same_v<std::remove_cvref_t<U>, in_place_t> &&
             !detail::is_failure_v<std::remove_cvref_t<U>>)
  constexpr result(U&& v) noexcept(std::is_nothrow_constructible_v<V, U>)
      : val_(static_cast<U&&>(v)), has_(true) {}

  // A void result's success carries no value.
  constexpr result() noexcept
    requires(is_void)
      : val_(), has_(true) {}

  // In-place value construction.
  template <class... A>
    requires(std::is_constructible_v<V, A...>)
  constexpr explicit result(in_place_t, A&&... a) noexcept(
      std::is_nothrow_constructible_v<V, A...>)
      : val_(static_cast<A&&>(a)...), has_(true) {}

  // Error construction from a failure.
  template <class E2 = E>
    requires(std::is_constructible_v<E, const E2&>)
  constexpr result(const failure<E2>& f) noexcept(
      std::is_nothrow_constructible_v<E, const E2&>)
      : err_(f.error()), has_(false) {}
  template <class E2 = E>
    requires(std::is_constructible_v<E, E2 &&>)
  constexpr result(failure<E2>&& f) noexcept(
      std::is_nothrow_constructible_v<E, E2&&>)
      : err_(static_cast<failure<E2>&&>(f).error()), has_(false) {}

  // Special members are conditionally trivial: a constrained `= default` is
  // selected when the contents allow it, a hand-written form otherwise, and a
  // deleted form when the operation is impossible.
  result(const result&)
    requires(triv_copy_ctor)
  = default;
  result(const result& o)
    requires(can_copy_ctor && !triv_copy_ctor)
      : has_(o.has_) {
    if (o.has_)
      ::new (static_cast<void*>(__builtin_addressof(val_))) V(o.val_);
    else
      ::new (static_cast<void*>(__builtin_addressof(err_))) E(o.err_);
  }
  result(const result&)
    requires(!can_copy_ctor)
  = delete;

  result(result&&)
    requires(triv_move_ctor)
  = default;
  result(result&& o)
  noexcept(std::is_nothrow_move_constructible_v<V> &&
           std::is_nothrow_move_constructible_v<E>)
    requires(can_move_ctor && !triv_move_ctor)
      : has_(o.has_) {
    if (o.has_)
      ::new (static_cast<void*>(__builtin_addressof(val_)))
          V(static_cast<V&&>(o.val_));
    else
      ::new (static_cast<void*>(__builtin_addressof(err_)))
          E(static_cast<E&&>(o.err_));
  }
  result(result&&)
    requires(!can_move_ctor)
  = delete;

  ~result()
    requires(triv_dtor)
  = default;
  constexpr ~result()
    requires(!triv_dtor)
  {
    destroy();
  }

  result& operator=(const result&)
    requires(triv_copy_assign)
  = default;
  result& operator=(const result& o)
    requires(can_copy_assign && !triv_copy_assign)
  {
    assign_from(o);
    return *this;
  }
  result& operator=(const result&)
    requires(!can_copy_assign)
  = delete;

  result& operator=(result&&)
    requires(triv_move_assign)
  = default;
  result& operator=(result&& o)
  noexcept(std::is_nothrow_move_constructible_v<V> &&
           std::is_nothrow_move_constructible_v<E> &&
           std::is_nothrow_move_assignable_v<V> &&
           std::is_nothrow_move_assignable_v<E>)
    requires(can_move_assign && !triv_move_assign)
  {
    assign_from(static_cast<result&&>(o));
    return *this;
  }
  result& operator=(result&&)
    requires(!can_move_assign)
  = delete;

  // Assigning a bare value or failure, when the transport is assignable. These
  // route through a temporary so the never-valueless assignment above is the
  // single place that handles a state transition.
  template <class U = T>
    requires(!is_void && std::is_constructible_v<V, U> &&
             std::is_move_assignable_v<result> &&
             !std::is_same_v<std::remove_cvref_t<U>, result> &&
             !detail::is_failure_v<std::remove_cvref_t<U>>)
  constexpr result& operator=(U&& v) {
    *this = result(in_place, static_cast<U&&>(v));
    return *this;
  }
  template <class E2 = E>
    requires(std::is_constructible_v<E, E2> && std::is_move_assignable_v<result>)
  constexpr result& operator=(failure<E2> f) {
    *this = result(static_cast<failure<E2>&&>(f));
    return *this;
  }

  [[nodiscard]] constexpr bool has_value() const noexcept { return has_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return has_;
  }
  [[nodiscard]] constexpr bool has_error() const noexcept { return !has_; }

  // Checked value access; defined (terminates) when holding a failure.
  constexpr decltype(auto) value() & {
    require_value();
    if constexpr (!is_void) return (val_);
  }
  constexpr decltype(auto) value() const& {
    require_value();
    if constexpr (!is_void) return (val_);
  }
  constexpr decltype(auto) value() && {
    require_value();
    if constexpr (!is_void) return detail::move(val_);
  }

  // Checked error access; defined (terminates) when holding a value.
  constexpr E& error() & {
    require_error();
    return err_;
  }
  constexpr const E& error() const& {
    require_error();
    return err_;
  }
  constexpr E&& error() && {
    require_error();
    return detail::move(err_);
  }

  // Narrow access: the precondition is that the corresponding state holds. Fast
  // hardening and above verify it; otherwise it is the caller's contract.
  constexpr decltype(auto) assume_value() & noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_value();
    if constexpr (!is_void) return (val_);
  }
  constexpr decltype(auto) assume_value() const& noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_value();
    if constexpr (!is_void) return (val_);
  }
  constexpr decltype(auto) assume_value() && noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_value();
    if constexpr (!is_void) return detail::move(val_);
  }
  constexpr E& assume_error() & noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_error();
    return err_;
  }
  constexpr const E& assume_error() const& noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_error();
    return err_;
  }
  constexpr E&& assume_error() && noexcept {
    if constexpr (JMPXX_HARDENING_FAST_ENABLED) require_error();
    return detail::move(err_);
  }

  // Pointer-like access to a held value (non-void), checked. The return types
  // are deduced so the members are never instantiated for result<void>.
  constexpr decltype(auto) operator->()
    requires(!is_void)
  {
    require_value();
    return detail::addressof(val_);
  }
  constexpr decltype(auto) operator->() const
    requires(!is_void)
  {
    require_value();
    return detail::addressof(val_);
  }
  constexpr decltype(auto) operator*() &
    requires(!is_void)
  {
    require_value();
    return (val_);
  }
  constexpr decltype(auto) operator*() const&
    requires(!is_void)
  {
    require_value();
    return (val_);
  }
  constexpr decltype(auto) operator*() &&
    requires(!is_void)
  {
    require_value();
    return detail::move(val_);
  }

  template <class U>
  constexpr T value_or(U&& alt) const&
    requires(!is_void && std::is_copy_constructible_v<T> &&
             std::is_convertible_v<U, T>)
  {
    return has_ ? val_ : static_cast<T>(static_cast<U&&>(alt));
  }
  template <class U>
  constexpr T value_or(U&& alt) &&
    requires(!is_void && std::is_move_constructible_v<T> &&
             std::is_convertible_v<U, T>)
  {
    return has_ ? detail::move(val_) : static_cast<T>(static_cast<U&&>(alt));
  }
  template <class G>
  constexpr E error_or(G&& alt) const& {
    return has_ ? static_cast<E>(static_cast<G&&>(alt)) : err_;
  }

  // Monadic composition, matching std::expected (P2505); the contract for each
  // combinator and for the callable it takes is in docs/reference/policies.md. The
  // call below is written out rather than routed through std::invoke because
  // <functional> is not in the freestanding subset, which is the one thing about
  // these four functions the code does not show on its own.

  // and_then: f itself returns a result; chain it on the value, propagate the failure.
  template <class F>
  constexpr auto and_then(F&& f) & {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      return has_ ? static_cast<F&&>(f)() : chained(fail(err_));
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(val_))>;
      return has_ ? static_cast<F&&>(f)(val_) : chained(fail(err_));
    }
  }
  template <class F>
  constexpr auto and_then(F&& f) const& {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      return has_ ? static_cast<F&&>(f)() : chained(fail(err_));
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(val_))>;
      return has_ ? static_cast<F&&>(f)(val_) : chained(fail(err_));
    }
  }
  template <class F>
  constexpr auto and_then(F&& f) && {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      return has_ ? static_cast<F&&>(f)() : chained(fail(detail::move(err_)));
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(detail::move(val_)))>;
      return has_ ? static_cast<F&&>(f)(detail::move(val_)) : chained(fail(detail::move(err_)));
    }
  }

  // transform: f returns a plain value; map it into a new result, propagate the failure.
  template <class F>
  constexpr auto transform(F&& f) & {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(); return result<void, E>(); }
        return result<void, E>(fail(err_));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)()) : result<chained, E>(fail(err_));
      }
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(val_))>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(val_); return result<void, E>(); }
        return result<void, E>(fail(err_));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)(val_)) : result<chained, E>(fail(err_));
      }
    }
  }
  template <class F>
  constexpr auto transform(F&& f) const& {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(); return result<void, E>(); }
        return result<void, E>(fail(err_));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)()) : result<chained, E>(fail(err_));
      }
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(val_))>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(val_); return result<void, E>(); }
        return result<void, E>(fail(err_));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)(val_)) : result<chained, E>(fail(err_));
      }
    }
  }
  template <class F>
  constexpr auto transform(F&& f) && {
    if constexpr (is_void) {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)())>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(); return result<void, E>(); }
        return result<void, E>(fail(detail::move(err_)));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)())
                    : result<chained, E>(fail(detail::move(err_)));
      }
    } else {
      using chained = std::remove_cvref_t<decltype(static_cast<F&&>(f)(detail::move(val_)))>;
      if constexpr (std::is_void_v<chained>) {
        if (has_) { static_cast<F&&>(f)(detail::move(val_)); return result<void, E>(); }
        return result<void, E>(fail(detail::move(err_)));
      } else {
        return has_ ? result<chained, E>(static_cast<F&&>(f)(detail::move(val_)))
                    : result<chained, E>(fail(detail::move(err_)));
      }
    }
  }

  // or_else: f returns a result on failure; pass a value through unchanged.
  template <class F>
  constexpr auto or_else(F&& f) & {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(err_))>;
    if constexpr (is_void) return has_ ? G() : static_cast<F&&>(f)(err_);
    else return has_ ? G(in_place, val_) : static_cast<F&&>(f)(err_);
  }
  template <class F>
  constexpr auto or_else(F&& f) const& {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(err_))>;
    if constexpr (is_void) return has_ ? G() : static_cast<F&&>(f)(err_);
    else return has_ ? G(in_place, val_) : static_cast<F&&>(f)(err_);
  }
  template <class F>
  constexpr auto or_else(F&& f) && {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(detail::move(err_)))>;
    if constexpr (is_void) return has_ ? G() : static_cast<F&&>(f)(detail::move(err_));
    else return has_ ? G(in_place, detail::move(val_)) : static_cast<F&&>(f)(detail::move(err_));
  }

  // transform_error: f returns a plain new error; map the error, pass a value through.
  template <class F>
  constexpr auto transform_error(F&& f) & {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(err_))>;
    if constexpr (is_void)
      return has_ ? result<void, G>() : result<void, G>(fail(static_cast<F&&>(f)(err_)));
    else
      return has_ ? result<T, G>(in_place, val_) : result<T, G>(fail(static_cast<F&&>(f)(err_)));
  }
  template <class F>
  constexpr auto transform_error(F&& f) const& {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(err_))>;
    if constexpr (is_void)
      return has_ ? result<void, G>() : result<void, G>(fail(static_cast<F&&>(f)(err_)));
    else
      return has_ ? result<T, G>(in_place, val_) : result<T, G>(fail(static_cast<F&&>(f)(err_)));
  }
  template <class F>
  constexpr auto transform_error(F&& f) && {
    using G = std::remove_cvref_t<decltype(static_cast<F&&>(f)(detail::move(err_)))>;
    if constexpr (is_void)
      return has_ ? result<void, G>()
                  : result<void, G>(fail(static_cast<F&&>(f)(detail::move(err_))));
    else
      return has_ ? result<T, G>(in_place, detail::move(val_))
                  : result<T, G>(fail(static_cast<F&&>(f)(detail::move(err_))));
  }

  friend constexpr bool operator==(const result& a, const result& b) {
    if (a.has_ != b.has_) return false;
    if (!a.has_) return a.err_ == b.err_;
    if constexpr (is_void)
      return true;
    else
      return a.val_ == b.val_;
  }
  template <class U>
  friend constexpr bool operator==(const result& a, const U& v)
    requires(!is_void && !detail::is_failure_v<U> &&
             !std::is_same_v<U, result>)
  {
    return a.has_ && a.val_ == v;
  }
  template <class E2>
  friend constexpr bool operator==(const result& a, const failure<E2>& f) {
    return !a.has_ && a.err_ == f.error();
  }

 private:
  constexpr void require_value() const {
    if (JMPXX_UNLIKELY(!has_))
      platform::fail_fast("jmpxx::result: value accessed on a failure");
  }
  constexpr void require_error() const {
    if (JMPXX_UNLIKELY(has_))
      platform::fail_fast("jmpxx::result: error accessed on a value");
  }
  constexpr void destroy() noexcept {
    if (has_)
      val_.~V();
    else
      err_.~E();
  }
  void assign_from(const result& o) {
    if (has_ && o.has_) {
      val_ = o.val_;
    } else if (!has_ && !o.has_) {
      err_ = o.err_;
    } else if (has_) {
      E incoming(o.err_);  // may throw; *this stays a value until it succeeds
      val_.~V();
      ::new (static_cast<void*>(__builtin_addressof(err_)))
          E(static_cast<E&&>(incoming));
      has_ = false;
    } else {
      V incoming(o.val_);
      err_.~E();
      ::new (static_cast<void*>(__builtin_addressof(val_)))
          V(static_cast<V&&>(incoming));
      has_ = true;
    }
  }
  void assign_from(result&& o) {
    if (has_ && o.has_) {
      val_ = static_cast<V&&>(o.val_);
    } else if (!has_ && !o.has_) {
      err_ = static_cast<E&&>(o.err_);
    } else if (has_) {
      E incoming(static_cast<E&&>(o.err_));
      val_.~V();
      ::new (static_cast<void*>(__builtin_addressof(err_)))
          E(static_cast<E&&>(incoming));
      has_ = false;
    } else {
      V incoming(static_cast<V&&>(o.val_));
      err_.~E();
      ::new (static_cast<void*>(__builtin_addressof(val_)))
          V(static_cast<V&&>(incoming));
      has_ = true;
    }
  }
};

}  // namespace jmpxx

#endif  // JMPXX_CORE_TRANSPORT_HPP
