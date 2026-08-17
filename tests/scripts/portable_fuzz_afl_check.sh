#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build the portable-surface stdin/file harness with AFL++ instrumentation and run
# afl-fuzz for a small deterministic budget. A crashing injected seed fails during
# AFL's dry run, which is the gate's inverted self-test.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/crash_hygiene.sh"
AFL_CXX="${1:?afl compiler}"; INC="${2:?include dir}"; OUT="${3:?output dir}"
SRC="${4:?source}"; SECS="${5:-6}"; MODE="${6:-clean}"
work="$OUT/portable_afl_${MODE}"
rm -rf "$work"
mkdir -p "$work/in" "$work/out"
printf '\x00\x01\x02\x03\x04\x05\x00payload' > "$work/in/seed"
defs=()
if [[ "$MODE" == "inject" ]]; then
  defs=(-DJMPXX_ADVERSARIAL_INJECT_DEFECT=1)
  printf 'JMPX' > "$work/in/injected"
fi
bin="$work/harness"
AFL_QUIET=1 "$AFL_CXX" -std=c++23 -O1 -g "${defs[@]}" \
  -DJMPXX_DIAGNOSTICS_ENABLED=1 -I "$INC" "$SRC" -o "$bin"
log="$work/afl.log"
set +e
AFL_SKIP_CPUFREQ=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 AFL_NO_UI=1 \
  afl-fuzz -V "$SECS" -i "$work/in" -o "$work/out" -- "$bin" @@ \
  >"$log" 2>&1
rc=$?
set -e
cat "$log"
if [[ "$MODE" == "inject" ]]; then
  if grep -q 'results in a crash' "$log"; then
    echo "AFL caught the injected crashing seed during dry run"
    exit 1
  fi
  echo "AFL did not catch the injected crashing seed"
  exit 0
fi
if [[ "$rc" -ne 0 ]]; then
  echo "AFL exited with $rc"
  exit "$rc"
fi
if find "$work/out" -path '*/crashes/id:*' -print -quit | grep -q .; then
  echo "AFL found a crash"
  exit 1
fi
echo "    cases.asked  1"
echo "    cases.known  1"
echo "portable AFL clean: ${SECS}s"
