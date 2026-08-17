# SPDX-License-Identifier: MIT
# Settings for a check whose subject is expected to die. Sourced, not executed.
#
# Several tiers run a program that must crash: an inverted self-test, a fail-fast
# precondition, a fuzz harness with an injected defect. Two host defaults turn that
# expected death into a large cost. A core pattern that pipes to a handler writes a crash
# image per run, and Ubuntu sets DEBUGINFOD_URLS system-wide, so a sanitizer report
# fetches debug info over the network for every frame: one injected libFuzzer crash took
# ninety seconds of wall time and almost no processor time, and a suite of such tiers
# running together exhausted the development host.
#
# Neither setting adds evidence. The check reads the exit status, and where a report is
# wanted, the clean run produces it with symbolization intact. Sourcing this keeps the
# expected deaths cheap and deterministic.
ulimit -c 0 2>/dev/null || true
export DEBUGINFOD_URLS=
