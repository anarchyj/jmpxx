<!-- SPDX-License-Identifier: MIT -->
# jmpxx-lint

Use `jmpxx-lint` when you want the library's usage discipline checked in consumer code. It is an
out-of-tree Clang tool built against the LLVM tooling libraries, distinct from the library and never
a runtime or include dependency: use it the way you use clang-tidy, not by linking it. It reads the
abstract syntax tree of a consumer translation unit and reports three usage mistakes.

## Checks

`jmpxx-discarded-result` flags a produced `result` whose value is dropped, either as a bare
expression statement or through a `(void)` cast that silences the `[[nodiscard]]` warning. A failure
must be inspected or propagated, not discarded.

`jmpxx-unchecked-access` flags a value taken through a narrow accessor, `assume_value` or
`assume_error`, with no success check on that result anywhere in the function. The narrow accessors
state a precondition the caller is responsible for, so an unchecked use is undefined behavior outside
a hardened build. The wide accessors `value` and `operator*` verify the state and terminate on
misuse, a defined event, so the check does not flag them.

`jmpxx-manual-propagation` flags a hand-written `if (!r) return fail(r.error());` that forwards a
result's failure, the pattern `JMPXX_TRY` expresses in one line.

## False positives

The checks are biased toward silence, because a check that flags correct code is worse than no
check. The unchecked-access check suppresses an access whenever the result is checked anywhere in the
function, which covers an early-return guard and any other check form, and it skips accesses a macro
synthesized, including `JMPXX_TRY`'s own. It therefore misses some real mistakes rather than ever
flagging a guarded access. Over the reference application it reports nothing, which is the intended
result for code that uses jmpxx correctly.

## Building

The tool needs the LLVM and Clang development libraries, version 18 on the development host. It is
built with the rest of the tree when they are present and is skipped when they are absent.

```sh
cmake -S . -B build -G Ninja \
  -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm -DClang_DIR=/usr/lib/llvm-18/lib/cmake/clang
cmake --build build --target jmpxx-lint
```

## Running

Pass a source file and, after `--`, the compile flags it is parsed with. No compilation database is
needed.

```sh
./build/lint/jmpxx-lint consumer.cpp -- -std=c++20 -fno-exceptions -fno-rtti -I/path/to/jmpxx/include
```

Each finding is one line, `file:line:col: check-name: message`, followed by a count, so a script or a
test can assert the exact set. The lint tier in continuous integration runs the tool over a fixture
of deliberate violations and asserts every check fires, and over a fixture of correct and
tricky-but-correct code and asserts it reports nothing.
