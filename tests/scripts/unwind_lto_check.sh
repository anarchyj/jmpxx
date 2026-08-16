#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Drive the unwind arm's destructor balance under whole-program and link-time
# optimization. The per-translation-unit tiers cannot see this: link-time optimization is
# where the optimizer first sees the landing, the intermediate frames, and the eject
# together, and where it could re-derive the property the arm depends on staying
# unprovable. Static linking is included because it changes how the unwinder finds the
# frame tables, and section garbage collection because it could drop them.
#
# Usage: unwind_lto_check.sh <cxx> <include-dir> <test-dir> <work-dir> [clean|inject] [runner]
# clean builds the shipped arm and every configuration must balance; inject builds the
# regressed arm from known_bad/ and the imbalance it causes must make this script fail,
# which is what gives the tier its teeth. A runner is the emulator a cross-built binary
# is executed through; naming one also links statically, so the emulated binary needs no
# target loader.
set -euo pipefail
CXX="${1:?compiler}"; INC="${2:?include dir}"; TESTS="${3:?test dir}"
OUT="${4:?work dir}"; MODE="${5:-clean}"; RUNNER="${6:-}"

work="$OUT/unwind_lto_${MODE}"
source "$(dirname "${BASH_SOURCE[0]}")/crash_hygiene.sh"
mkdir -p "$work"

defect_flags=()
case "$MODE" in
  clean) ;;
  inject) defect_flags=(-DJMPXX_TEST_REGRESSED_ARM=1 -DJMPXX_KNOWN_BAD_NOTHROW_EJECT=1) ;;
  *) echo "unknown mode: $MODE" >&2; exit 2 ;;
esac

is_clang=0
if "$CXX" --version 2>/dev/null | grep -qi clang; then is_clang=1; fi

# The ARM exception-handling ABI spells a C++ cleanup's exit as __cxa_end_cleanup rather
# than a direct _Unwind_Resume, and under link-time optimization the compiler emits the
# direct call instead. A native throw survives that; a forced unwind of a foreign
# exception does not, and terminates as the second frame's cleanup finishes. The
# combination is therefore unsupported, and because no preprocessor macro identifies
# -flto, the header cannot refuse it. Rather than skip the cell, the check pins the
# limitation: on this ABI the ordinary build must still work and the link-time-optimized
# build must still fail, so a toolchain that fixes it is noticed rather than assumed.
# __ARM_EABI_UNWINDER__ is defined by <unwind.h>, not predefined by the compiler, so the
# model is read by compiling a probe rather than by asking for the macro list.
is_ehabi=0
if printf '#include <unwind.h>\n#ifndef __ARM_EABI_UNWINDER__\n#error dwarf\n#endif\nint main(){}\n' \
   | "$CXX" -x c++ -std=c++20 -fsyntax-only - 2>/dev/null; then
  is_ehabi=1
fi

# Each configuration is a label followed by the flags that distinguish it. Thin
# link-time optimization is a Clang mode; whole-program is the GCC counterpart.
# Apple's linker is not GNU ld. It has no --gc-sections, spelling the same idea
# -dead_strip, and it cannot produce a fully static executable at all, because the
# platform ships no crt0.o and static libSystem is unsupported. Both of those are
# properties of the platform rather than of the arm, so a build failure there would
# report a destructor imbalance that never happened. The equivalent configuration is
# substituted where one exists, and the one with no equivalent is named below rather
# than dropped quietly.
is_darwin=0
[[ "$(uname -s 2>/dev/null)" == "Darwin" ]] && is_darwin=1

skipped=()
configs=(
  "lto_O2|-O2|-flto"
  "lto_O3|-O3|-flto"
)
if [[ "$is_darwin" -eq 1 ]]; then
  skipped+=("lto_static: this platform cannot link a fully static executable")
  configs+=("lto_dead_strip|-O2|-flto|-ffunction-sections|-fdata-sections|-Wl,-dead_strip")
else
  configs+=("lto_static|-O2|-flto|-static")
  configs+=("lto_gc_sections|-O2|-flto|-ffunction-sections|-fdata-sections|-Wl,--gc-sections")
fi
if [[ "$is_clang" -eq 1 ]]; then
  configs+=("thinlto_O2|-O2|-flto=thin")
else
  configs+=("lto_whole_program|-O2|-flto|-fwhole-program")
fi

failures=0
report="$work/report.txt"
: > "$report"

link_flags=()
[[ -n "$RUNNER" ]] && link_flags=(-static)

build_and_run() {  # label, exe, flags...
  local label="$1"; local exe="$2"; shift 2
  if ! "$CXX" -std=c++20 "$@" ${link_flags[@]+"${link_flags[@]}"} \
        ${defect_flags[@]+"${defect_flags[@]}"} \
        -I "$INC" -I "$TESTS/unwind" \
        "$TESTS/unwind/lto_frames.cpp" "$TESTS/unwind/lto_landing.cpp" \
        -o "$exe" 2> "$work/$label.build.log"; then
    return 125
  fi
  ${RUNNER:+$RUNNER} "$exe" > "$work/$label.run.log" 2>&1
}

if [[ "$is_ehabi" -eq 1 ]]; then
  set +e
  build_and_run ehabi_plain "$work/ehabi_plain" -O2
  rc=$?
  if [[ "$rc" -eq 0 ]]; then
    echo "ehabi_without_lto: PASS" | tee -a "$report"
  else
    echo "ehabi_without_lto: FAIL (exit $rc)" | tee -a "$report"
    sed 's/^/    /' "$work/ehabi_plain.run.log" 2>/dev/null | head -8
    failures=$((failures + 1))
  fi

  build_and_run ehabi_lto "$work/ehabi_lto" -O2 -flto
  rc=$?
  set -e
  if [[ "$rc" -eq 0 ]]; then
    echo "ehabi_with_lto: PASS, which the recorded limitation says is impossible" | tee -a "$report"
    echo "the toolchain now emits __cxa_end_cleanup under link-time optimization;" \
         "re-measure the limitation and update the reference before relying on it"
    failures=$((failures + 1))
  else
    echo "ehabi_with_lto: refused as recorded (exit $rc)" | tee -a "$report"
  fi

  if [[ "$failures" -ne 0 ]]; then
    echo "unwind link-time optimization: the ARM exception-handling ABI cell did not match its recorded limitation"
    exit 1
  fi
  echo "unwind link-time optimization: the ARM exception-handling ABI cell behaves as recorded, ordinary build correct and the link-time-optimized build refused"
  exit 0
fi

for spec in "${configs[@]}"; do
  IFS='|' read -r label flags <<< "${spec%%|*}|${spec#*|}"
  IFS='|' read -r -a flagv <<< "${spec#*|}"
  exe="$work/$label"
  set +e
  build_and_run "$label" "$exe" "${flagv[@]}"
  rc=$?
  set -e
  if [[ "$rc" -eq 125 ]]; then
    echo "$label: BUILD FAILED" | tee -a "$report"
    tail -3 "$work/$label.build.log" || true
    failures=$((failures + 1))
    continue
  fi
  if [[ "$rc" -eq 0 ]]; then
    echo "$label: PASS" | tee -a "$report"
  else
    echo "$label: FAIL (exit $rc)" | tee -a "$report"
    sed 's/^/    /' "$work/$label.run.log" | head -12
    failures=$((failures + 1))
  fi
done

if [[ "${#skipped[@]}" -ne 0 ]]; then
  for s in "${skipped[@]}"; do echo "$s: UNAVAILABLE ON THIS PLATFORM" | tee -a "$report"; done
fi

if [[ "$failures" -ne 0 ]]; then
  echo "unwind link-time optimization: $failures configuration(s) did not keep the arm's destructor balance"
  exit 1
fi
echo "unwind link-time optimization: ${#configs[@]} configuration(s) ran each destructor exactly once, ${#skipped[@]} unavailable on this platform"
