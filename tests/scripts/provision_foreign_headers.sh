#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Fetch the upstream header trees the foreign-consumer tier compiles the public surface
# behind.
#
# The tier's subject is a name a real project defines, not a name this project thought
# to transcribe. A transcribed list only ever covers what its author remembered, and the
# collision that made the library uncompilable for firmware consumers was found by a
# real tree rather than by a list. So the trees are the upstream ones, pinned to a tag,
# and only their headers are kept.
#
# Nothing fetched here reaches the library, the tests' link line, or an installed
# target: the trees are include paths for a compile-only check.
#
# Usage: provision_foreign_headers.sh <root>
set -euo pipefail
ROOT="${1:?root directory for the fetched header trees}"
mkdir -p "$ROOT"

fetch() {
  local name="$1" url="$2" dir="$3"
  if [ -d "$ROOT/$name" ]; then
    echo "$name: already provisioned"
    return 0
  fi
  echo "$name: fetching $url"
  local tmp
  tmp="$(mktemp -d)"
  if ! curl -sSL --max-time 300 -o "$tmp/src.tar.gz" "$url"; then
    echo "$name: fetch failed"
    rm -rf "$tmp"
    return 1
  fi
  tar xzf "$tmp/src.tar.gz" -C "$tmp"
  mkdir -p "$ROOT/$name"
  cp -r "$tmp/$dir/." "$ROOT/$name/"
  rm -rf "$tmp"
  echo "$name: provisioned into $ROOT/$name"
}

# The secure-firmware convention: short uppercase function-like macros in the namespace
# a C++ header would otherwise use for identifiers. U, UL, ULL, BIT, MIN, MAX and
# ARRAY_SIZE all come from here.
fetch trusted-firmware-a \
  https://codeload.github.com/ARM-software/arm-trusted-firmware/tar.gz/refs/tags/v2.11.0 \
  arm-trusted-firmware-2.11.0/include

# The embedded-RTOS convention, which overlaps the firmware one and differs in what it
# spells: MIN, MAX, CLAMP, ARRAY_SIZE, CONTAINER_OF, IS_ENABLED and BIT.
fetch zephyr \
  https://codeload.github.com/zephyrproject-rtos/zephyr/tar.gz/refs/tags/v3.7.0 \
  zephyr-3.7.0/include

echo "foreign header trees under $ROOT:"
ls -1 "$ROOT"
