#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build and run the portable surface under the meaningful hardening, diagnostics,
# NDEBUG, stack-trace, exceptions, and RTTI combinations. The stack-trace cell runs
# under ASan/UBSan so the capture path is observed live under sanitizers.
set -euo pipefail
CXX="${1:?compiler}"; INC="${2:?include dir}"; OUT="${3:?output dir}"
SRC="${4:?source}"; MODE="${5:-clean}"
work="$OUT/config_matrix_${MODE}"
rm -rf "$work"
mkdir -p "$work"
count=0

run_cell() {
  local name="$1"; shift
  local exe="$work/$name"
  "$CXX" -std=c++23 -O1 -g -I "$INC" "$SRC" "$@" -o "$exe"
  "$exe" > "$work/$name.out"
  count=$((count + 1))
}

run_cell release_none_noexc_nortti \
  -DNDEBUG -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_NONE \
  -DJMPXX_DIAGNOSTICS_ENABLED=0 -fno-exceptions -fno-rtti
run_cell debug_fast_diag_exc_rtti \
  -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_FAST -DJMPXX_DIAGNOSTICS_ENABLED=1
run_cell release_diag_on \
  -DNDEBUG -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_FAST \
  -DJMPXX_DIAGNOSTICS_ENABLED=1
run_cell extensive_noexc_nortti \
  -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE \
  -DJMPXX_DIAGNOSTICS_ENABLED=1 -fno-exceptions -fno-rtti
run_cell none_diag_on_nortti \
  -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_NONE \
  -DJMPXX_DIAGNOSTICS_ENABLED=1 -fno-rtti
run_cell fast_diag_off_noexc_rtti \
  -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_FAST \
  -DJMPXX_DIAGNOSTICS_ENABLED=0 -fno-exceptions
run_cell stacktrace_sanitized \
  -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_FAST \
  -DJMPXX_DIAGNOSTICS_ENABLED=1 -DJMPXX_STACKTRACE=1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
if [[ "$MODE" != "inject" ]]; then
  run_cell release_extensive_diag_stacktrace \
    -DNDEBUG -DJMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE \
    -DJMPXX_DIAGNOSTICS_ENABLED=1 -DJMPXX_STACKTRACE=1
fi

expected=8
if [[ "$count" -ne "$expected" ]]; then
  echo "configuration matrix ran $count cells, expected $expected"
  exit 1
fi
cat "$work"/*.out
echo "    cases.asked  $count"
echo "    cases.known  $expected"
echo "configuration matrix clean: $count cells"
