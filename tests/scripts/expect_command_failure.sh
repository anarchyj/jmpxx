#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Run a command that is expected to fail, including by signal, and convert that
# observed failure into an ordinary nonzero exit for CTest WILL_FAIL.
set -euo pipefail
set +e
source "$(dirname "${BASH_SOURCE[0]}")/crash_hygiene.sh"
"$@"
rc=$?
set -e
if [[ "$rc" -eq 0 ]]; then
  echo "expected command to fail, but it exited 0"
  exit 0
fi
echo "expected failure observed (exit $rc)"
exit 1
