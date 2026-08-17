// SPDX-License-Identifier: MIT
// The experimental, opt-in non-local unwind arm. A failure ejected deep in a recursion
// returns to a single landing while the platform unwinder runs every destructor on the
// path, so the intermediate frames carry no propagation construct.
//
// The arm is not the default. It requires unwind cleanup tables, so this translation
// unit is built with exceptions enabled, unlike the portable, exception-free core. Reach
// it only by including jmpxx/unwind.hpp. The portable result and JMPXX_TRY remain the
// right choice for strict exception-free code; see docs/reference/unwind.md.
#include <jmpxx/core.hpp>
#include <jmpxx/unwind.hpp>

#include <cstdio>
#include <initializer_list>

using jmpxx::error;

// A recursive descent that ejects on a malformed node. The recursive calls neither test
// nor thread an error; a failure deep inside escapes straight to the landing.
int sum_digits(const char* s, int depth) {
  if (depth > 64) jmpxx::unwind::eject(error(1));  // runaway nesting
  if (*s == '\0') return 0;
  if (*s < '0' || *s > '9') jmpxx::unwind::eject(error(2));  // not a digit
  return (*s - '0') + sum_digits(s + 1, depth + 1);
}

int main() {
  if (!jmpxx::unwind::available()) {
    std::printf("the unwind arm has no backend on this target; using the portable surface\n");
    return 0;
  }

  for (const char* in : {"1234", "12x4"}) {
    // The landing boundary. The body returns normally on success; an eject below lands here.
    auto r = jmpxx::unwind::escape_scope<error>([&] { return sum_digits(in, 0); });
    if (r)
      std::printf("sum_digits(\"%s\") = %d\n", in, r.value());
    else
      std::printf("sum_digits(\"%s\") ejected: code=%d\n", in, r.error().code);
  }
  return 0;
}
