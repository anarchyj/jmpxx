#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the source-mutant campaign over the portable public headers."""

from __future__ import annotations

import argparse
import dataclasses
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


@dataclasses.dataclass(frozen=True)
class Mutant:
    ident: str
    file: str
    before: str
    after: str
    region: str
    occurrence: int = 1
    equivalent_reason: str | None = None


@dataclasses.dataclass(frozen=True)
class SuiteCase:
    name: str
    source: str
    definitions: tuple[str, ...] = ()
    flags: tuple[str, ...] = ()
    args: tuple[str, ...] = ()


MUTANTS = [
    Mutant(
        "transport.has_value_inverted",
        "include/jmpxx/core/transport.hpp",
        "[[nodiscard]] constexpr bool has_value() const noexcept { return has_; }",
        "[[nodiscard]] constexpr bool has_value() const noexcept { return !has_; }",
        "transport discriminant",
    ),
    Mutant(
        "transport.has_error_inverted",
        "include/jmpxx/core/transport.hpp",
        "[[nodiscard]] constexpr bool has_error() const noexcept { return !has_; }",
        "[[nodiscard]] constexpr bool has_error() const noexcept { return has_; }",
        "transport discriminant",
    ),
    Mutant(
        "transport.require_value_inverted",
        "include/jmpxx/core/transport.hpp",
        "if (JMPXX_UNLIKELY(!has_))\n      platform::fail_fast(\"jmpxx::result: value accessed on a failure\");",
        "if (JMPXX_UNLIKELY(has_))\n      platform::fail_fast(\"jmpxx::result: value accessed on a failure\");",
        "checked value access",
    ),
    Mutant(
        "transport.require_error_inverted",
        "include/jmpxx/core/transport.hpp",
        "if (JMPXX_UNLIKELY(has_))\n      platform::fail_fast(\"jmpxx::result: error accessed on a value\");",
        "if (JMPXX_UNLIKELY(!has_))\n      platform::fail_fast(\"jmpxx::result: error accessed on a value\");",
        "checked error access",
    ),
    Mutant(
        "transport.copy_value_to_error_keeps_value",
        "include/jmpxx/core/transport.hpp",
        "      has_ = false;",
        "      has_ = true;",
        "copy assignment cross-state discriminant",
        occurrence=1,
    ),
    Mutant(
        "transport.copy_error_to_value_keeps_error",
        "include/jmpxx/core/transport.hpp",
        "      has_ = true;",
        "      has_ = false;",
        "copy assignment cross-state discriminant",
        occurrence=1,
    ),
    Mutant(
        "transport.move_value_to_error_keeps_value",
        "include/jmpxx/core/transport.hpp",
        "      has_ = false;",
        "      has_ = true;",
        "move assignment cross-state discriminant",
        occurrence=2,
    ),
    Mutant(
        "transport.move_error_to_value_keeps_error",
        "include/jmpxx/core/transport.hpp",
        "      has_ = true;",
        "      has_ = false;",
        "move assignment cross-state discriminant",
        occurrence=2,
    ),
    Mutant(
        "transport.value_or_branch_inverted",
        "include/jmpxx/core/transport.hpp",
        "return has_ ? val_ : static_cast<T>(static_cast<U&&>(alt));",
        "return !has_ ? val_ : static_cast<T>(static_cast<U&&>(alt));",
        "value_or branch",
        occurrence=1,
    ),
    Mutant(
        "transport.error_or_branch_inverted",
        "include/jmpxx/core/transport.hpp",
        "return has_ ? static_cast<E>(static_cast<G&&>(alt)) : err_;",
        "return !has_ ? static_cast<E>(static_cast<G&&>(alt)) : err_;",
        "error_or branch",
    ),
    Mutant(
        "propagation.try_value_branch_inverted",
        "include/jmpxx/core/propagation.hpp",
        "if (JMPXX_UNLIKELY(!tmp))",
        "if (JMPXX_UNLIKELY(tmp))",
        "JMPXX_TRY branch",
    ),
    Mutant(
        "propagation.tryv_branch_inverted",
        "include/jmpxx/core/propagation.hpp",
        "if (JMPXX_UNLIKELY(!_jmpxx_v))",
        "if (JMPXX_UNLIKELY(_jmpxx_v))",
        "JMPXX_TRYV branch",
    ),
    Mutant(
        "erased.generic_fold_operator_changed",
        "include/jmpxx/erased.hpp",
        "return static_cast<int>(static_cast<unsigned>(code) ^\n                          (static_cast<unsigned>(domain_tag) << 16));",
        "return static_cast<int>(static_cast<unsigned>(code) +\n                          (static_cast<unsigned>(domain_tag) << 16));",
        "type-erased policy fold",
    ),
    Mutant(
        "erased.generic_fold_shift_changed",
        "include/jmpxx/erased.hpp",
        "(static_cast<unsigned>(domain_tag) << 16)",
        "(static_cast<unsigned>(domain_tag) << 15)",
        "type-erased policy fold",
    ),
    Mutant(
        "erased.null_domain_guard_inverted",
        "include/jmpxx/erased.hpp",
        "if (JMPXX_UNLIKELY(domain_ == nullptr))",
        "if (JMPXX_UNLIKELY(domain_ != nullptr))",
        "extensive hardening boundary check",
    ),
    Mutant(
        "diagnostics.open_capacity_off_by_one",
        "include/jmpxx/diagnostic/store.hpp",
        "if (depth_ >= max_inflight) return id;",
        "if (depth_ > max_inflight) return id;",
        "diagnostic store capacity",
    ),
    Mutant(
        "diagnostics.hop_capacity_off_by_one",
        "include/jmpxx/diagnostic/store.hpp",
        "if (r->hop_count >= max_chain) {",
        "if (r->hop_count > max_chain) {",
        "diagnostic hop capacity",
    ),
    Mutant(
        "diagnostics.hop_count_not_incremented",
        "include/jmpxx/diagnostic/store.hpp",
        "r->hops[r->hop_count++] = loc;",
        "r->hops[0] = loc;",
        "diagnostic hop append",
    ),
    Mutant(
        "diagnostics.truncate_zero_mark_ignored",
        "include/jmpxx/diagnostic/store.hpp",
        "if (m >= 0 && m < depth_) depth_ = m;",
        "if (m > 0 && m < depth_) depth_ = m;",
        "diagnostic landing truncation",
    ),
    Mutant(
        "diagnostics.find_misses_oldest_record",
        "include/jmpxx/diagnostic/store.hpp",
        "for (int i = depth_ - 1; i >= 0; --i)",
        "for (int i = depth_ - 1; i > 0; --i)",
        "diagnostic handle lookup",
    ),
    Mutant(
        "diagnostics.propagation_hook_erased",
        "include/jmpxx/diagnostics.hpp",
        "diagnostic::detail::tls().add_hop(e.handle(), loc);",
        "(void)e; (void)loc;",
        "diagnostic propagation hook",
    ),
]


FULL_SUITE = [
    SuiteCase("static_asserts", "tests/static/static_asserts.cpp"),
    SuiteCase("constexpr", "tests/constexpr/constexpr_tests.cpp"),
    SuiteCase("property", "tests/property/property_tests.cpp"),
    SuiteCase("monadic_noexc_nortti", "tests/monadic/monadic_tests.cpp",
              flags=("-fno-exceptions", "-fno-rtti")),
    SuiteCase("policy_behavior", "tests/policy/behavior.cpp",
              definitions=("-DJMPXX_DIAGNOSTICS_ENABLED=1",)),
    SuiteCase("interop_roundtrip", "tests/interop/roundtrip.cpp"),
    SuiteCase("structured_adversarial", "tests/adversarial/structured_sequences.cpp",
              definitions=("-DJMPXX_DIAGNOSTICS_ENABLED=1",
                           "-DJMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE")),
    SuiteCase("differential_oracle", "tests/adversarial/differential_oracle.cpp"),
    SuiteCase("exception_safety", "tests/adversarial/exception_safety.cpp"),
    SuiteCase("model_lifecycle", "tests/adversarial/model_lifecycle.cpp",
              definitions=("-DJMPXX_DIAGNOSTICS_ENABLED=1",
                           "-DJMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE")),
    SuiteCase("usage_scenarios", "tests/adversarial/usage_scenarios.cpp",
              definitions=("-DJMPXX_DIAGNOSTICS_ENABLED=0",
                           "-DJMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE"),
              flags=("-fno-exceptions", "-fno-rtti")),
]

WEAKENED_SUITE = [
    SuiteCase("static_asserts", "tests/static/static_asserts.cpp"),
]


def run(cmd: list[str], timeout: int) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, timeout=timeout)


def replace_nth(text: str, before: str, after: str, occurrence: int) -> str:
    start = -1
    cursor = 0
    for _ in range(occurrence):
        start = text.find(before, cursor)
        if start < 0:
            raise ValueError("mutation target not found")
        cursor = start + len(before)
    return text[:start] + after + text[start + len(before):]


def prepare_include_tree(source_root: Path, work: Path, mutant: Mutant | None) -> Path:
    include_root = work / "include"
    shutil.copytree(source_root / "include", include_root)
    if not mutant:
        return include_root

    target = work / mutant.file
    text = target.read_text()
    target.write_text(replace_nth(text, mutant.before, mutant.after, mutant.occurrence))
    return include_root


def run_suite(cxx: str, source_root: Path, include_root: Path, work: Path,
              suite: list[SuiteCase]) -> tuple[bool, dict[str, str]]:
    case_dir = work / "cases"
    case_dir.mkdir(parents=True, exist_ok=True)
    for case in suite:
        exe = case_dir / case.name
        cmd = [
            cxx, "-std=c++23", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-fno-sanitize-recover=all", "-I", str(include_root),
            *case.definitions, *case.flags, str(source_root / case.source),
            "-o", str(exe),
        ]
        built = run(cmd, timeout=60)
        if built.returncode != 0:
            return False, {
                "case": case.name,
                "phase": "compile",
                "returncode": str(built.returncode),
                "output": built.stderr[-2000:] or built.stdout[-2000:],
            }
        executed = run([str(exe), *case.args], timeout=60)
        if executed.returncode != 0:
            return False, {
                "case": case.name,
                "phase": "run",
                "returncode": str(executed.returncode),
                "output": executed.stderr[-2000:] or executed.stdout[-2000:],
            }
    return True, {}


def campaign(args: argparse.Namespace) -> dict[str, object]:
    source_root = Path(args.source_root).resolve()
    build_root = Path(args.build_root).resolve()
    build_root.mkdir(parents=True, exist_ok=True)
    suite = WEAKENED_SUITE if args.mode == "inject" else FULL_SUITE

    with tempfile.TemporaryDirectory(prefix="jmpxx_mutation_", dir=build_root) as tmp:
        tmp_path = Path(tmp)
        clean_include = prepare_include_tree(source_root, tmp_path / "clean", None)
        clean_ok, clean_failure = run_suite(args.cxx, source_root, clean_include,
                                            tmp_path / "clean", suite)
        if not clean_ok:
            return {
                "ok": False,
                "reason": "clean suite failed before mutation",
                "failure": clean_failure,
                "suite_cases": [case.name for case in suite],
            }

        results = []
        killed = 0
        equivalent = 0
        for mutant in MUTANTS:
            work = tmp_path / mutant.ident
            include_root = prepare_include_tree(source_root, work, mutant)
            survived, failure = run_suite(args.cxx, source_root, include_root, work, suite)
            killed_here = not survived
            if killed_here:
                killed += 1
            elif mutant.equivalent_reason:
                equivalent += 1
            results.append({
                "id": mutant.ident,
                "region": mutant.region,
                "status": "killed" if killed_here else
                          "equivalent" if mutant.equivalent_reason else "survived",
                "killed_by": failure.get("case") if killed_here else None,
                "phase": failure.get("phase") if killed_here else None,
                "triage": mutant.equivalent_reason,
            })

    denominator = len(MUTANTS) - equivalent
    kill_rate = killed / denominator if denominator else 1.0
    untriaged = [r["id"] for r in results if r["status"] == "survived"]
    covered_regions = sorted({m.region for m in MUTANTS})
    ok = kill_rate >= args.floor and not untriaged
    if args.mode == "inject":
        ok = False if kill_rate < args.floor else True
    return {
        "ok": ok,
        "mode": args.mode,
        "floor": args.floor,
        "kill_rate": kill_rate,
        "killed": killed,
        "equivalent": equivalent,
        "total": len(MUTANTS),
        "covered_regions": covered_regions,
        "suite_cases": [case.name for case in suite],
        "untriaged_survivors": untriaged,
        "mutants": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="jmpxx source-mutant campaign")
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--build-root", required=True)
    parser.add_argument("--print-config", action="store_true",
                        help="print the committed kill-rate floor as JSON and exit")
    parser.add_argument("--floor", type=float, default=0.90)
    parser.add_argument("--mode", choices=("clean", "inject"), default="clean")
    parser.add_argument("--report")
    # The committed kill-rate floor, readable without a build, so the documentation
    # that states it is checked against this gate's own configuration.
    if "--print-config" in sys.argv:
        defaults = parser.parse_args(["--cxx", "", "--source-root", "",
                                      "--build-root", ""])
        print(json.dumps({"floor": defaults.floor}))
        return 0
    args = parser.parse_args()

    report = campaign(args)
    text = json.dumps(report, indent=2, sort_keys=True)
    if args.report:
        Path(args.report).write_text(text + "\n")
    # What the campaign mutated, against the population it holds. A campaign that
    # quietly stops mutating part of the surface still kills everything it tried.
    print(f"    cases.asked  {len(report.get('mutants', []))}")
    print(f"    cases.known  {len(MUTANTS)}")
    print(text)
    if report.get("ok"):
        print("mutation campaign clean")
        return 0
    print("mutation campaign failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
