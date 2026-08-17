// SPDX-License-Identifier: MIT
// Exception-safety cross-product tier for non-trivial result assignment. The
// transport must never expose a valueless state: throwing same-state assignments
// leave the active alternative selected, throwing cross-state construction happens
// before the old alternative is destroyed, and unsafe cross-state assignments whose
// reconstruction could throw are removed from the type surface.
#include "jmpxx/core.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>

using namespace jmpxx;

namespace {

[[noreturn]] void die(const char* what) {
  std::fprintf(stderr, "exception-safety: %s\n", what);
  std::abort();
}

struct controls {
  static inline bool copy_ctor = false;
  static inline bool copy_assign = false;
  static inline bool move_assign = false;
  static void reset() noexcept { copy_ctor = copy_assign = move_assign = false; }
};

struct checked_error {
  int code = 0;
  explicit checked_error(int c = 0) noexcept : code(c) {}
  checked_error(const checked_error& o) : code(o.code) {
    if (controls::copy_ctor) throw 1;
  }
  checked_error(checked_error&& o) noexcept : code(o.code) {}
  checked_error& operator=(const checked_error& o) {
    if (controls::copy_assign) throw 2;
    code = o.code;
    return *this;
  }
  checked_error& operator=(checked_error&& o) {
    if (controls::move_assign) throw 3;
    code = o.code;
    return *this;
  }
};

struct checked_value {
  int value = 0;
  explicit checked_value(int v = 0) noexcept : value(v) {}
  checked_value(const checked_value& o) : value(o.value) {
    if (controls::copy_ctor) throw 4;
  }
  checked_value(checked_value&& o) noexcept : value(o.value) {}
  checked_value& operator=(const checked_value& o) {
    if (controls::copy_assign) throw 5;
    value = o.value;
    return *this;
  }
  checked_value& operator=(checked_value&& o) {
    if (controls::move_assign) throw 6;
    value = o.value;
    return *this;
  }
};

struct throwing_move_ctor {
  int value = 0;
  explicit throwing_move_ctor(int v = 0) noexcept : value(v) {}
  throwing_move_ctor(const throwing_move_ctor&) = default;
  throwing_move_ctor(throwing_move_ctor&&) noexcept(false) { throw 7; }
  throwing_move_ctor& operator=(const throwing_move_ctor&) = default;
  throwing_move_ctor& operator=(throwing_move_ctor&&) = default;
};

static_assert(std::is_copy_assignable_v<result<checked_value, checked_error>>);
static_assert(std::is_move_assignable_v<result<checked_value, checked_error>>);
static_assert(!std::is_copy_assignable_v<result<throwing_move_ctor, checked_error>>);
static_assert(!std::is_move_assignable_v<result<throwing_move_ctor, checked_error>>);
static_assert(!std::is_copy_assignable_v<result<checked_value, throwing_move_ctor>>);
static_assert(!std::is_move_assignable_v<result<checked_value, throwing_move_ctor>>);

template <class F>
void must_throw(F&& f, const char* what) {
  bool caught = false;
  try {
    static_cast<F&&>(f)();
  } catch (...) {
    caught = true;
  }
  if (!caught) die(what);
}

void copy_assign_same_value_throw_keeps_value() {
  controls::reset();
  result<checked_value, checked_error> a(in_place, 1);
  result<checked_value, checked_error> b(in_place, 2);
  controls::copy_assign = true;
  must_throw([&] { a = b; }, "value copy assignment did not throw");
  if (!a.has_value()) die("value copy assignment lost the value state");
}

void copy_assign_same_error_throw_keeps_error() {
  controls::reset();
  result<checked_value, checked_error> a = jmpxx::fail(checked_error(1));
  result<checked_value, checked_error> b = jmpxx::fail(checked_error(2));
  controls::copy_assign = true;
  must_throw([&] { a = b; }, "error copy assignment did not throw");
  if (a.has_value()) die("error copy assignment lost the error state");
}

void move_assign_same_value_throw_keeps_value() {
  controls::reset();
  result<checked_value, checked_error> a(in_place, 1);
  result<checked_value, checked_error> b(in_place, 2);
  controls::move_assign = true;
  must_throw([&] { a = static_cast<result<checked_value, checked_error>&&>(b); },
             "value move assignment did not throw");
  if (!a.has_value()) die("value move assignment lost the value state");
}

void move_assign_same_error_throw_keeps_error() {
  controls::reset();
  result<checked_value, checked_error> a = jmpxx::fail(checked_error(1));
  result<checked_value, checked_error> b = jmpxx::fail(checked_error(2));
  controls::move_assign = true;
  must_throw([&] { a = static_cast<result<checked_value, checked_error>&&>(b); },
             "error move assignment did not throw");
  if (a.has_value()) die("error move assignment lost the error state");
}

void copy_cross_value_to_error_throw_keeps_value() {
  controls::reset();
  result<checked_value, checked_error> a(in_place, 1);
  result<checked_value, checked_error> b = jmpxx::fail(checked_error(2));
  controls::copy_ctor = true;
  must_throw([&] { a = b; }, "value-to-error copy assignment did not throw");
  if (!a.has_value() || a.value().value != 1)
    die("value-to-error throw changed the old value state");
}

void copy_cross_error_to_value_throw_keeps_error() {
  controls::reset();
  result<checked_value, checked_error> a = jmpxx::fail(checked_error(1));
  result<checked_value, checked_error> b(in_place, 2);
  controls::copy_ctor = true;
  must_throw([&] { a = b; }, "error-to-value copy assignment did not throw");
  if (a.has_value() || a.error().code != 1)
    die("error-to-value throw changed the old error state");
}

void move_cross_success_paths_reconstruct_after_destroy() {
  controls::reset();
  result<checked_value, checked_error> a(in_place, 1);
  result<checked_value, checked_error> b = jmpxx::fail(checked_error(2));
  a = static_cast<result<checked_value, checked_error>&&>(b);
  if (a.has_value() || a.error().code != 2)
    die("value-to-error move assignment did not land on the error");

  result<checked_value, checked_error> c = jmpxx::fail(checked_error(3));
  result<checked_value, checked_error> d(in_place, 4);
  c = static_cast<result<checked_value, checked_error>&&>(d);
  if (!c.has_value() || c.value().value != 4)
    die("error-to-value move assignment did not land on the value");
}

}  // namespace

int main(int argc, char** argv) {
  bool inject = argc > 1 && std::string_view(argv[1]) == "--inject-valueless";
  copy_assign_same_value_throw_keeps_value();
  copy_assign_same_error_throw_keeps_error();
  move_assign_same_value_throw_keeps_value();
  move_assign_same_error_throw_keeps_error();
  copy_cross_value_to_error_throw_keeps_value();
  copy_cross_error_to_value_throw_keeps_error();
  move_cross_success_paths_reconstruct_after_destroy();
  if (inject) die("injected observable-valueless defect");
  std::printf("    cases.asked  %d\n    cases.known  %d\n", 7, 7);
  std::printf("exception-safety: throwing assignment cross-product clean\n");
  return 0;
}
