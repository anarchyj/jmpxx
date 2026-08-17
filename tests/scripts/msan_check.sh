#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build and run a MemorySanitizer tier against an MSan-instrumented libc++ root.
# The root is provisioned outside the repository and passed by CMake/CI; using an
# uninstrumented system libstdc++ is not accepted for this gate.
set -euo pipefail
CXX="${1:?compiler}"; INC="${2:?include dir}"; OUT="${3:?output dir}"
SRC="${4:?source}"; MODE="${5:-clean}"; ROOT="${6:-${JMPXX_MSAN_LIBCXX_ROOT:-}}"
if [[ -z "$ROOT" ]]; then
  echo "JMPXX_MSAN_LIBCXX_ROOT is required for the memory-sanitizer gate"
  exit 2
fi
if [[ ! -d "$ROOT/include/c++/v1" ]]; then
  echo "MSan libc++ headers not found under $ROOT/include/c++/v1"
  exit 2
fi
libcpp="$(find "$ROOT" \( -name 'libc++.a' -o -name 'libc++.so' \) -print -quit)"
libabi="$(find "$ROOT" \( -name 'libc++abi.a' -o -name 'libc++abi.so' \) -print -quit)"
if [[ -z "$libcpp" || -z "$libabi" ]]; then
  echo "MSan libc++ libraries not found under $ROOT"
  exit 2
fi
libdir="$(dirname "$libcpp")"
abidir="$(dirname "$libabi")"
work="$OUT/msan_${MODE}"
mkdir -p "$work"
bin="$work/$(basename "$SRC").msan"
"$CXX" -std=c++20 -O1 -g -fsanitize=memory -fsanitize-memory-track-origins=2 \
  -fno-omit-frame-pointer -fno-sanitize-recover=all -stdlib=libc++ -nostdinc++ \
  -I "$ROOT/include/c++/v1" -I "$INC" -L "$libdir" -L "$abidir" \
  -Wl,-rpath,"$libdir" -Wl,-rpath,"$abidir" \
  -DJMPXX_DIAGNOSTICS_ENABLED=1 "$SRC" -lc++abi -pthread -o "$bin"
MSAN_OPTIONS=halt_on_error=1:exit_code=99 "$bin"
echo "    cases.asked  1"
echo "    cases.known  1"
echo "memory sanitizer clean for $(basename "$SRC")"
