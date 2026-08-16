#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build the portable-surface libFuzzer harness and run it for a bounded CI budget.
# With the optional "inject" argument the harness contains a deliberate defect and
# the seed corpus includes the triggering input; that run must fail.
set -euo pipefail
CXX="${1:?compiler}"; INC="${2:?include dir}"; OUT="${3:?output dir}"
SRC="${4:?source}"; SECS="${5:-10}"; MODE="${6:-clean}"
work="$OUT/portable_libfuzzer_${MODE}"
source "$(dirname "${BASH_SOURCE[0]}")/crash_hygiene.sh"
mkdir -p "$work/corpus"
printf '\x00\x01\x02\x03\x04\x05\x00payload' > "$work/corpus/seed"
defs=()
if [[ "$MODE" == "inject" ]]; then
  defs=(-DJMPXX_ADVERSARIAL_INJECT_DEFECT=1)
  printf 'JMPX' > "$work/corpus/injected"
fi
bin="$work/harness"
"$CXX" -std=c++23 -O1 -g -fsanitize=fuzzer,address,undefined \
  -fno-omit-frame-pointer -fno-sanitize-recover=all "${defs[@]}" \
  -DJMPXX_DIAGNOSTICS_ENABLED=1 -I "$INC" "$SRC" -o "$bin"
# The artifact prefix keeps a crashing input inside the work directory. Without it
# libFuzzer writes crash-<hash> into the current directory, which is the repository root
# when the suite runs from there, so the injected run would leave a file behind.
"$bin" "$work/corpus" -max_total_time="$SECS" -error_exitcode=99 \
  -artifact_prefix="$work/" -timeout=10 -rss_limit_mb=2048
echo "portable libFuzzer clean: ${SECS}s"
