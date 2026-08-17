#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build the public surface behind real upstream header trees, in the order a consumer
# produces: their headers first, jmpxx after.
#
# Each ecosystem here takes identifiers a C++ header would otherwise use and makes them
# macros, and each does it differently: the firmware trees take short uppercase names,
# the Windows headers take ordinary English words in lower case, and the X11 headers
# take capitalized words that read as type names. A hand-written list of macros only
# ever covers the ones its author remembered, which is why the collision that made the
# library uncompilable for firmware consumers survived until a real tree was on the
# include path.
#
# A tree that is absent is reported absent rather than passed over, and the check fails
# if fewer ecosystems than the recorded floor were covered. A compile error inside an
# upstream tree is that tree's problem, not this library's, and is reported as such.
#
# Usage: foreign_headers_check.sh <cxx> <include-dir> <work-dir> <trees-root> [clean|inject]
set -uo pipefail
CXX="${1:?host C++ compiler}"
INC="${2:?jmpxx include dir}"
WORK="${3:?work dir}"
TREES="${4:-}"
MODE="${5:-clean}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURES="$HERE/../interop/foreign"
mkdir -p "$WORK"

# Ecosystems, each with the compiler that targets it, the include flags its tree needs,
# a runner, and the identifier the tree defines that the inverted case makes a public
# header reach for.
covered=0
absent=0
upstream=0
failures=0
known=4

report() { printf '  %-22s %s\n' "$1" "$2"; }

# The inverted case: a public header that reaches for a name the ecosystem's tree
# defines. The header tree is copied first, so the repository is untouched.
inject_include_tree() {
  local name="$1" out="$2"
  rm -rf "$out"
  cp -r "$INC" "$out"
  cat >> "$out/jmpxx/core/error.hpp" <<EOF

namespace jmpxx {
// Injected by the foreign-header tier: a public name this ecosystem's headers define
// as a macro. Behind those headers this declaration is not what it says.
inline int $name(int v) { return v; }
}  // namespace jmpxx
EOF
}

try_ecosystem() {
  local label="$1" cxx="$2" fixture="$3" collide="$4" runner="$5"
  shift 5
  local flags=("$@")

  if ! command -v "$cxx" >/dev/null 2>&1; then
    report "$label" "SKIP: $cxx is not installed"
    absent=$((absent + 1))
    return
  fi

  local inc="$INC"
  if [ "$MODE" = "inject" ]; then
    inc="$WORK/${label}_injected_include"
    inject_include_tree "$collide" "$inc"
  fi

  # A Windows binary is named with its extension, which its runner expects.
  local exe="$WORK/$label"
  [ "$runner" = "wine" ] && exe="$exe.exe"
  local log="$WORK/$label.log"
  # An empty array expands to an unbound variable under set -u on bash 3.2, which is
  # what macOS ships, so an ecosystem that needs no extra flags must expand guarded.
  if "$cxx" -std=c++20 -O1 ${flags[@]+"${flags[@]}"} -I"$inc" -I"$FIXTURES" \
       "$fixture" -o "$exe" 2>"$log"; then
    if [ -n "$runner" ] && ! command -v "$runner" >/dev/null 2>&1; then
      report "$label" "built; $runner is absent so it was not run"
      covered=$((covered + 1))
      return
    fi
    if WINEDEBUG=-all ${runner:+$runner} "$exe" >/dev/null 2>&1; then
      report "$label" "the public surface builds and runs behind these headers"
      covered=$((covered + 1))
    else
      report "$label" "built behind these headers and did not run correctly"
      failures=$((failures + 1))
    fi
    return
  fi

  # An ecosystem whose own headers are not installed here is absent, not failing. The
  # fixture never reached the tree, so nothing about this library was put to the test,
  # and reporting it as a failure would say the surface broke where it was never tried.
  #
  # What is matched is the missing include's own name rather than the line carrying it,
  # because the line also carries the path of the file that did the including, and that
  # path runs through a directory named for this project.
  local missing
  missing="$(grep -oE "fatal error: '[^']+' file not found|fatal error: [^:]+: No such file or directory" \
             "$log" | head -1)"
  if [ -n "$missing" ] && ! printf '%s' "$missing" | grep -q "jmpxx"; then
    report "$label" "SKIP: this ecosystem's headers are not installed here ($missing)"
    absent=$((absent + 1))
    return
  fi

  # A failure inside the upstream tree is the tree's, not this library's. It is only
  # ours when an error names a jmpxx header or the fixture that includes it.
  if grep -q "error:" "$log" && \
     ! grep "error:" "$log" | grep -qE "jmpxx|foreign/|${label}_injected_include"; then
    report "$label" "SKIP: this tree does not compile here; the errors are its own"
    sed -n '1,3p' "$log" | sed 's/^/        /'
    upstream=$((upstream + 1))
    return
  fi
  report "$label" "the public surface does not build behind these headers"
  grep -m3 "error:" "$log" | sed 's/^/        /'
  failures=$((failures + 1))
}

tfa_flags=()
[ -n "$TREES" ] && [ -d "$TREES/trusted-firmware-a" ] && \
  tfa_flags=(-I"$TREES/trusted-firmware-a")
zephyr_flags=()
[ -n "$TREES" ] && [ -d "$TREES/zephyr" ] && zephyr_flags=(-I"$TREES/zephyr")

if [ ${#tfa_flags[@]} -eq 0 ]; then
  report "trusted-firmware-a" "SKIP: tree absent; run provision_foreign_headers.sh"
  absent=$((absent + 1))
else
  try_ecosystem trusted-firmware-a "$CXX" "$FIXTURES/firmware.cpp" U "" "${tfa_flags[@]}"
fi
if [ ${#zephyr_flags[@]} -eq 0 ]; then
  report "zephyr" "SKIP: tree absent; run provision_foreign_headers.sh"
  absent=$((absent + 1))
else
  try_ecosystem zephyr "$CXX" "$FIXTURES/zephyr.cpp" BIT "" "${zephyr_flags[@]}"
fi

# The Windows and X11 trees arrive with a toolchain and a system package rather than
# from a fetch, so they cost nothing to keep in the set.
try_ecosystem windows x86_64-w64-mingw32-g++ "$FIXTURES/windows.cpp" interface wine -static
try_ecosystem x11 "$CXX" "$FIXTURES/x11.cpp" Status ""

floor=2
# An ecosystem this host cannot reach is not a case the gate declined to ask; it is a
# case that does not exist here. It leaves the known population and is reported as
# absent, so the two counts agree while the summary still says what was out of reach.
echo "    cases.asked  $((covered + failures + upstream))"
echo "    cases.known  $((known - absent))"
echo "foreign header trees: $covered covered, $absent absent, $upstream skipped as "\
"upstream problems, $failures failed"

if [ "$failures" -ne 0 ]; then
  echo "FAIL: the public surface does not survive an ecosystem's own headers"
  exit 1
fi
if [ "$covered" -lt "$floor" ]; then
  echo "FAIL: $covered ecosystems covered, below the floor of $floor; the trees must be"\
       "on the include path for this check to mean anything"
  exit 1
fi
echo "foreign headers OK: the public surface builds and runs behind $covered ecosystems"
