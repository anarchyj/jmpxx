#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build a libFuzzer target with the address and undefined-behavior sanitizers and
# run it for a bounded time, so the failure-construction and bridge machinery are
# exercised against random input with every undefined behavior turned into a crash.
# libFuzzer is a Clang facility, so this tier runs on Clang. A crash or sanitizer
# finding fails the run; a clean bounded run passes.
set -euo pipefail
CXX="${1:?compiler}"; INC="${2:?include dir}"; OUT="${3:?output dir}"
SRC="${4:?source}"; SECS="${5:-15}"
bin="$OUT/$(basename "$SRC").fuzz"
"$CXX" -std=c++23 -O1 -g -fsanitize=fuzzer,address,undefined \
  -fno-omit-frame-pointer -fno-sanitize-recover=all -I "$INC" "$SRC" -o "$bin"
# A crashing input is written under the output directory rather than the current one,
# which is the repository root when the suite runs from there.
"$bin" -max_total_time="$SECS" -error_exitcode=99 -artifact_prefix="$OUT/" \
  -timeout=10 -rss_limit_mb=2048
echo "fuzz clean: no crash or sanitizer finding in ${SECS}s over the interop boundary"
