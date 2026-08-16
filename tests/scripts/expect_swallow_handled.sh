#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Verify the arm handles a swallowing catch-all on the escape path safely on whatever C++
# runtime the build uses. There are two safe outcomes and one defect. The runtime may
# detect the swallow and fail-fast, where the fixture terminates by a signal, or it may
# transit the catch-all so the escape reaches its landing, where the fixture exits 0. The
# defect is the escape being silently lost so the scope returns a value the program never
# produced, which the fixture reports with a small non-zero exit code. Pass on a clean
# exit or a signal, fail on a plain non-zero exit.
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/crash_hygiene.sh"
"$@"
rc=$?
if [ "$rc" -eq 0 ] || [ "$rc" -ge 128 ]; then
  echo "swallow handled safely (detected or transited; rc=$rc)"
  exit 0
fi
echo "FAIL: a swallowed escape was mishandled (rc=$rc)"
exit 1
