#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Check the arm's reentrancy contract case by case. Each case has one declared outcome:
# it either lands, meaning the process exits 0 with its own checks passed, or it
# terminates, meaning the arm refused the escape and the process died by a signal. A case
# that lands where it must terminate is the failure this tier exists to catch, because
# that is the shape a silent wrong landing takes.
#
# Usage: unwind_reentrancy_check.sh <fixture> [clean|inject] [runner]
# inject passes --fake-success to the fixture, which makes every refusing case return
# success instead, and the driver must then fail. runner prefixes each invocation, so a
# cross-architecture cell passes its emulator here and gets the same expectations and the
# same teeth as the native cell rather than a loop of its own.
#
# Note for a caller: this script deliberately does not set -e. Most cases here are
# expected to die by a signal, so under -e the first correct refusal would end the script
# before its outcome was read. A caller that runs it from a shell with -e is unaffected,
# because the failure is reported through the exit status below.
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/crash_hygiene.sh"
FIXTURE="${1:?reentrancy fixture}"; MODE="${2:-clean}"; RUNNER="${3:-}"

extra=()
[[ "$MODE" == "inject" ]] && extra=(--fake-success)

# case name, expected outcome
expectations=(
  "nested_scope_in_cleanup terminate"
  "nested_scope_success_in_cleanup land"
  "reeject_in_cleanup terminate"
  "no_active_scope terminate"
  "type_mismatch terminate"
  "after_scope_returned terminate"
  "other_thread_has_the_scope terminate"
)

failures=0
for spec in "${expectations[@]}"; do
  set -- $spec
  name="$1"; want="$2"
  out="$(${RUNNER:+$RUNNER} "$FIXTURE" "$name" ${extra[@]+"${extra[@]}"} 2>&1)"
  rc=$?
  if [[ "$rc" -ge 128 ]]; then got="terminate"; elif [[ "$rc" -eq 0 ]]; then got="land"; else got="fail"; fi
  printf '  %-32s want=%-9s got=%-9s (exit %d)\n' "$name" "$want" "$got" "$rc"
  [[ -n "$out" ]] && printf '%s\n' "$out" | sed 's/^/      /'
  if [[ "$got" != "$want" ]]; then failures=$((failures + 1)); fi
done

if [[ "$failures" -ne 0 ]]; then
  echo "unwind reentrancy: $failures case(s) did not produce their declared outcome"
  exit 1
fi
echo "unwind reentrancy: every case landed or terminated as declared"
