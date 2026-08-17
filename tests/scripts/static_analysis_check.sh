#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Run Clang Static Analyzer over a public-header translation unit. scan-build's
# --status-bugs turns analyzer findings into a nonzero exit, so the known-bad null
# dereference is the gate's inverted self-test.
set -euo pipefail
SCAN_BUILD="${1:?scan-build}"; CXX="${2:?compiler}"; INC="${3:?include dir}"
OUT="${4:?output dir}"; SRC="${5:?source}"
work="$OUT/static_analysis_$(basename "$SRC")"
rm -rf "$work"
mkdir -p "$work"
"$SCAN_BUILD" --status-bugs -o "$work/report" \
  "$CXX" -std=c++23 -I "$INC" -fsyntax-only "$SRC"
echo "    cases.asked  1"
echo "    cases.known  1"
echo "static analysis clean for $(basename "$SRC")"
