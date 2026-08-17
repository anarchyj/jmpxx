#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# The deterministic perf gate. callgrind counts the instructions a region executes from a
# simulated CPU, so the count is identical on every machine and every run, unlike a wall-clock
# time. The gate runs the jmpxx and the hand-written kernels under callgrind over an identical
# call loop and bounds the jmpxx instruction count to a small multiple of the hand-written one,
# so a jmpxx chain that executes more instructions than the branch it replaces fails the build.
# The deliberately slowed kernel runs in the same script and must exceed the bound, so a gate
# that would pass a real regression fails here first.
#
# An instruction count is not a cycle count. callgrind models no pipeline, cache, or branch
# prediction, so this gate checks instruction parity on the measured region, not the wall-clock
# sad-path cost, which the distribution benchmark measures.
#
# The run also records the per-call count for every mechanism the comparison names, on
# both the happy and the failure path, into a report the documented-value gate reads.
# The comparison table and this gate then take their numbers from one measurement
# rather than from two copies that can drift.
set -euo pipefail
BENCH="${1:?jmpxx-bench path}"
VG="${2:-valgrind}"
ITERS="${3:-200000}"
BOUND_PCT="${4:-110}"  # jmpxx instructions must be <= this percent of hand-written
REPORT="${5:-}"        # optional JSON report of every mechanism's per-call count

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

ir_for() {
  local mech="$1" fail="${2:-0}"
  local cg="$work/cg.$mech.$fail"
  "$VG" --tool=callgrind --collect-atstart=no --callgrind-out-file="$cg" \
    "$BENCH" callgrind --mech "$mech" --fail "$fail" --iters "$ITERS" >/dev/null 2>&1
  # With collection deferred and toggled around the region, the summary line holds the region's
  # instruction total (a single Ir event by default).
  awk '/^summary:/{print $2; exit}' "$cg"
}

jx="$(ir_for jmpxx 0)"
hw="$(ir_for handwritten 0)"
slow="$(ir_for jmpxx_slow 0)"

# The mechanisms the comparison page tabulates. A mechanism the suite was built without
# reports nothing and is recorded as absent rather than as a zero.
MECHS="handwritten jmpxx std_expected tl_expected boost_outcome std_error_code boost_leaf exceptions"
if [ -n "$REPORT" ]; then
  printf '{\n "tool": "jmpxx-callgrind-ir",\n "schema": 1,\n "iters": %s,\n "per_call": {\n' \
    "$ITERS" > "$REPORT"
  first=1
  measured=0
  for mech in $MECHS; do
    happy="$(ir_for "$mech" 0)"
    sad="$(ir_for "$mech" 1)"
    [ -n "$happy" ] || continue
    measured=$((measured+1))
    [ $first -eq 1 ] || printf ',\n' >> "$REPORT"
    first=0
    printf '  "%s": {"happy": %s, "failure": %s}' \
      "$mech" "$((happy / ITERS))" "$((sad / ITERS))" >> "$REPORT"
  done
  printf '\n }\n}\n' >> "$REPORT"
  known=0
  for mech in $MECHS; do known=$((known+1)); done
  echo "    cases.asked  $measured"
  echo "    cases.known  $known"
  if [ "$measured" -ne "$known" ]; then
    echo "FAIL: measured $measured mechanisms and the comparison names $known"
    exit 1
  fi
  echo "per-mechanism instruction counts written to $REPORT"
fi

if [ -z "$jx" ] || [ -z "$hw" ] || [ -z "$slow" ]; then
  echo "FAIL: could not read an instruction total from callgrind output"
  exit 1
fi

echo "instructions over $ITERS happy-path calls:"
echo "  jmpxx        = $jx  (per call ~ $((jx / ITERS)))"
echo "  handwritten  = $hw  (per call ~ $((hw / ITERS)))"
echo "  jmpxx_slow   = $slow (per call ~ $((slow / ITERS)))"

# Gate: jmpxx within BOUND_PCT% of hand-written.
if [ "$((100 * jx))" -gt "$((BOUND_PCT * hw))" ]; then
  echo "FAIL: jmpxx instructions $jx exceed ${BOUND_PCT}% of hand-written $hw"
  exit 1
fi
# Inverted self-test: the slowed kernel must trip the same bound.
if [ "$((100 * slow))" -le "$((BOUND_PCT * hw))" ]; then
  echo "FAIL(inverted): the slowed kernel did not exceed the bound"
  exit 1
fi

echo "callgrind-ir OK: jmpxx within ${BOUND_PCT}% of hand-written; the slowed kernel trips the gate"
