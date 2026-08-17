// SPDX-License-Identifier: MIT
// Single-construct failure propagation, offered at two levels.
//
// Level 1, checked propagation, is the zero-overhead default. JMPXX_TRYV and
// JMPXX_TRY evaluate a result and, if it holds a failure, return that failure
// from the current function; on success JMPXX_TRY binds the produced value.
// Each adds a single predictable branch and nothing else, which is the cost a
// committed codegen golden pins. JMPXX_TRYX is the same operation usable inside
// an expression; it relies on the statement-expression extension and so is
// available on GCC and Clang only, with JMPXX_TRY as the portable form.
//
// Level 2, the landing scope, marks the single typed boundary a region's
// propagation lands at. It introduces a call boundary, which costs one frame
// unless the call is inlined, and allocates nothing; its reference entry states
// that cost.
#ifndef JMPXX_CORE_PROPAGATION_HPP
#define JMPXX_CORE_PROPAGATION_HPP

#include "jmpxx/core/config.hpp"
#include "jmpxx/core/diagnostic_hook.hpp"
#include "jmpxx/core/transport.hpp"

#include <type_traits>

#define JMPXX_DETAIL_CAT2(a, b) a##b
#define JMPXX_DETAIL_CAT(a, b) JMPXX_DETAIL_CAT2(a, b)
#define JMPXX_DETAIL_UNIQUE(base) JMPXX_DETAIL_CAT(base, __COUNTER__)

// Hop hook. On the failure path a propagation records its site with the
// diagnostic layer; for the minimal policy and in release this is nothing. It is
// a comma operand on the existing return so it adds no statement and no block,
// which keeps the diagnostics-off expansion byte-identical to a bare return and
// the zero-overhead codegen golden unchanged.
#if JMPXX_DIAGNOSTICS_ENABLED
#define JMPXX_DETAIL_HOP(err) ::jmpxx::detail::note_propagation(err),
#else
#define JMPXX_DETAIL_HOP(err)
#endif

// Propagate a failure, discarding the value on success. The enclosing function
// must return a result whose error type accepts this one.
#define JMPXX_TRYV(expr)                                               \
  do {                                                                 \
    auto&& _jmpxx_v = (expr);                                          \
    if (JMPXX_UNLIKELY(!_jmpxx_v))                                     \
      return (JMPXX_DETAIL_HOP(_jmpxx_v.assume_error())::jmpxx::fail(  \
          static_cast<decltype(_jmpxx_v)&&>(_jmpxx_v).assume_error()));\
  } while (false)

// Propagate a failure, and on success declare `auto name` bound to the value,
// moved out of the evaluated expression.
#define JMPXX_TRY(name, expr) \
  JMPXX_DETAIL_TRY(name, expr, JMPXX_DETAIL_UNIQUE(jmpxx_try_tmp_))
#define JMPXX_DETAIL_TRY(name, expr, tmp)                                  \
  auto&& tmp = (expr);                                                     \
  if (JMPXX_UNLIKELY(!tmp))                                                \
    return (JMPXX_DETAIL_HOP(tmp.assume_error())::jmpxx::fail(             \
        static_cast<decltype(tmp)&&>(tmp).assume_error()));               \
  auto name = static_cast<decltype(tmp)&&>(tmp).assume_value()

#if JMPXX_COMPILER_GCC || JMPXX_COMPILER_CLANG
// Value-yielding propagation usable inside an expression. The statement
// expression yields a freshly constructed value (a prvalue), so nothing refers
// to the evaluated temporary after it is destroyed. The statement-expression
// extension is GCC and Clang only, asked for through the platform-unit tokens so
// the platform fence stays clean.
#define JMPXX_TRYX(expr)                                                     \
  ({                                                                        \
    auto&& _jmpxx_x = (expr);                                               \
    if (JMPXX_UNLIKELY(!_jmpxx_x))                                          \
      return (JMPXX_DETAIL_HOP(_jmpxx_x.assume_error())::jmpxx::fail(       \
          static_cast<decltype(_jmpxx_x)&&>(_jmpxx_x).assume_error()));     \
    typename ::std::remove_cvref_t<decltype(_jmpxx_x)>::value_type(         \
        static_cast<decltype(_jmpxx_x)&&>(_jmpxx_x).assume_value());        \
  })
#define JMPXX_HAS_TRYX 1
#endif

namespace jmpxx {

template <class Body>
[[nodiscard]] constexpr auto try_scope(Body&& body)
    -> decltype(static_cast<Body&&>(body)()) {
  return static_cast<Body&&>(body)();
}

}  // namespace jmpxx

#endif  // JMPXX_CORE_PROPAGATION_HPP
