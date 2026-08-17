#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# The escape's body must not run in the landing frame.
#
# The landing plants a jump buffer, so an object constructed in the landing frame after
# that point is not destroyed by the resuming longjmp. A non-inlinable trampoline keeps
# the body's objects in a frame the unwind destroys, and this check runs a deep escape
# at every optimization level and requires the destructor count to balance.
#
# It also runs the falsification attempt this defence has never survived being given: a
# copy of the include tree in which the trampoline is forced inline, which is what the
# defence exists to prevent. That attempt does not reproduce a lost destructor on the
# compilers here, so the defence is recorded as one that could not be falsified rather
# than as one with a known-bad case behind it. The attempt is re-run every time, so a
# compiler that starts breaking it is noticed rather than assumed.
#
# Usage: unwind_body_frame_check.sh <cxx> <include-dir> <fixture> <work-dir>
set -uo pipefail
CXX="${1:?C++ compiler}"
INC="${2:?jmpxx include dir}"
FIXTURE="${3:?destructor-count fixture}"
WORK="${4:?work dir}"
mkdir -p "$WORK"

ulimit -c 0 2>/dev/null || true
export DEBUGINFOD_URLS=

levels="0 1 2 3"
asked=0
known=8
failures=0

# The defence in place: the ordinary build must balance at every level.
for opt in $levels; do
  asked=$((asked + 1))
  exe="$WORK/body_frame_O$opt"
  if ! "$CXX" -std=c++20 "-O$opt" -I "$INC" "$FIXTURE" -o "$exe" 2>"$WORK/build.log"; then
    echo "  O$opt: BUILD FAILED"
    sed -n '1,3p' "$WORK/build.log" | sed 's/^/      /'
    failures=$((failures + 1))
    continue
  fi
  if "$exe" >"$WORK/run.O$opt.log" 2>&1; then
    echo "  O$opt: the escape destroyed every object it constructed"
  else
    echo "  O$opt: FAILED"
    sed -n '1,5p' "$WORK/run.O$opt.log" | sed 's/^/      /'
    failures=$((failures + 1))
  fi
done

# The falsification attempt: the same subject with the trampoline forced into the
# landing frame. A tree copy, so the repository is untouched.
inlined="$WORK/forced_inline_include"
rm -rf "$inlined"
cp -r "$INC" "$inlined"
# The attribute is rewritten rather than removed, so the body is forced into the
# landing frame instead of merely being allowed there. One other use of the same macro
# is already spelled with inline, so the substitution names only the attribute.
sed -i 's/#define JMPXX_NOINLINE __attribute__((noinline))/#define JMPXX_NOINLINE __attribute__((always_inline)) inline/' \
  "$inlined/jmpxx/core/config.hpp"
sed -i 's/JMPXX_NOINLINE inline void drive_unwind/JMPXX_NOINLINE void drive_unwind/' \
  "$inlined/jmpxx/unwind/backend.hpp"
if ! grep -q "always_inline" "$inlined/jmpxx/core/config.hpp"; then
  echo "FAIL: the falsification attempt could not be set up; the attribute it rewrites moved"
  exit 1
fi

reproduced=0
for opt in $levels; do
  asked=$((asked + 1))
  exe="$WORK/forced_inline_O$opt"
  if ! "$CXX" -std=c++20 "-O$opt" -I "$inlined" "$FIXTURE" -o "$exe" 2>"$WORK/inline.build.log"; then
    echo "  O$opt forced inline: did not build"
    continue
  fi
  if "$exe" >"$WORK/inline.run.O$opt.log" 2>&1; then
    echo "  O$opt forced inline: still balanced, so this attempt did not falsify the defence"
  else
    echo "  O$opt forced inline: LOST A DESTRUCTOR, so the defence is load-bearing here"
    reproduced=$((reproduced + 1))
  fi
done

echo "    cases.asked  $asked"
echo "    cases.known  $known"
if [ "$failures" -ne 0 ]; then
  echo "FAIL: the escape did not destroy every object the body constructed"
  exit 1
fi
if [ "$reproduced" -ne 0 ]; then
  echo "unwind body frame: the trampoline is load-bearing on this compiler, which makes"\
       "this defence falsifiable; record it with teeth rather than as unfalsifiable"
  exit 0
fi
echo "unwind body frame: balanced at every level, and forcing the body inline did not"\
     "reproduce a loss on this compiler, which is why this defence is recorded as one"\
     "that could not be falsified"
