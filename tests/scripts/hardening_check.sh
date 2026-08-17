#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Verify the graded hardening mode. Fast and extensive builds must fail-fast on
# their checks, none must not carry the narrow-access trap in generated code, and
# extensive-only domain validation must be absent below extensive.
set -euo pipefail
CXX="${1:?compiler}"; INC="${2:?include dir}"; OUT="${3:?output dir}"
SRC_FAST="${4:?wrong-state source}"; SRC_EXT="${5:?erased-null source}"
MODE="${6:-clean}"
work="$OUT/hardening_${MODE}"
source "$(dirname "${BASH_SOURCE[0]}")/crash_hygiene.sh"
mkdir -p "$work"

compile_asm() {
  local src="$1"; local mode="$2"; local asm="$3"
  "$CXX" -std=c++20 -O2 -S -fno-exceptions -fno-rtti \
    -DJMPXX_HARDENING_MODE="$mode" -I "$INC" "$src" -o "$asm"
}

# Every expectation below is one case. The count is reported beside the number of
# cases this gate knows of, so a gate that quietly stops checking something is visible
# without being red.
cases=0

expect_ud2() {
  local asm="$1"; local label="$2"
  cases=$((cases+1))
  if ! grep -qE '(^|[[:space:]])ud2($|[[:space:]])' "$asm"; then
    echo "$label: expected a fail-fast trap in generated code"
    exit 1
  fi
}

expect_no_ud2() {
  local asm="$1"; local label="$2"
  cases=$((cases+1))
  if grep -qE '(^|[[:space:]])ud2($|[[:space:]])' "$asm"; then
    echo "$label: unexpected fail-fast trap in generated code"
    exit 1
  fi
}

expect_fails() {
  local exe="$1"; local label="$2"
  cases=$((cases+1))
  set +e
  "$exe" >/dev/null 2>&1
  local rc=$?
  set -e
  if [[ "$rc" -eq 0 ]]; then
    echo "$label: expected fail-fast exit"
    exit 1
  fi
}

expect_compile_fails() {
  local label="$1"
  cases=$((cases+1))
  shift
  set +e
  "$@" >/dev/null 2>"$work/${label}.err"
  local rc=$?
  set -e
  if [[ "$rc" -eq 0 ]]; then
    echo "$label: expected compilation to fail"
    exit 1
  fi
}

compile_asm "$SRC_FAST" JMPXX_HARDENING_NONE "$work/fast_none.s"
compile_asm "$SRC_FAST" JMPXX_HARDENING_FAST "$work/fast_fast.s"
compile_asm "$SRC_FAST" JMPXX_HARDENING_EXTENSIVE "$work/fast_extensive.s"
expect_no_ud2 "$work/fast_none.s" "none narrow access"
expect_ud2 "$work/fast_fast.s" "fast narrow access"
expect_ud2 "$work/fast_extensive.s" "extensive narrow access"

compile_asm "$SRC_EXT" JMPXX_HARDENING_FAST "$work/ext_fast.s"
compile_asm "$SRC_EXT" JMPXX_HARDENING_EXTENSIVE "$work/ext_extensive.s"
expect_no_ud2 "$work/ext_fast.s" "fast erased-domain dispatch"
expect_ud2 "$work/ext_extensive.s" "extensive erased-domain dispatch"

"$CXX" -std=c++20 -O1 -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_FAST \
  -I "$INC" "$SRC_FAST" -o "$work/wrong_state_fast"
expect_fails "$work/wrong_state_fast" "fast wrong-state narrow access"

"$CXX" -std=c++20 -O1 -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE \
  -I "$INC" "$SRC_EXT" -o "$work/erased_extensive"
expect_fails "$work/erased_extensive" "extensive null erased-domain dispatch"

"$CXX" -std=c++20 -O1 -DJMPXX_HARDENED=1 -I "$INC" "$SRC_FAST" \
  -o "$work/binary_on"
expect_fails "$work/binary_on" "binary hardening on"
"$CXX" -std=c++20 -O1 -DJMPXX_HARDENED=1 \
  -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_FAST -I "$INC" "$SRC_FAST" \
  -o "$work/compatible_binary_on"
"$CXX" -std=c++20 -O2 -S -fno-exceptions -fno-rtti -DJMPXX_HARDENED=0 \
  -I "$INC" "$SRC_FAST" -o "$work/binary_off.s"
expect_no_ud2 "$work/binary_off.s" "binary hardening off"
expect_compile_fails "conflicting_hardened_macros" \
  "$CXX" -std=c++20 -O1 -DJMPXX_HARDENED=0 \
  -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_FAST -I "$INC" "$SRC_FAST" \
  -o "$work/conflicting_binary"

if [[ "$MODE" == "inject" ]]; then
  expect_no_ud2 "$work/fast_fast.s" "injected bad expectation for fast mode"
fi

known=10
[[ "$MODE" == "inject" ]] && known=11
echo "    cases.asked  $cases"
echo "    cases.known  $known"
if [[ "$cases" -ne "$known" ]]; then
  echo "hardening: ran $cases expectations, the gate knows of $known"
  exit 1
fi
echo "hardening modes clean: none/fast/extensive fire and vanish as expected"
