#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Measure adversarial coverage over the portable public headers."""

from __future__ import annotations

import argparse
import dataclasses
import json
import subprocess
import sys
import tempfile
from pathlib import Path


@dataclasses.dataclass(frozen=True)
class CoverageCase:
    name: str
    source: str
    definitions: tuple[str, ...] = ()
    flags: tuple[str, ...] = ()
    args: tuple[str, ...] = ()


@dataclasses.dataclass(frozen=True)
class CriticalRegion:
    name: str
    file: str
    start_anchor: str
    end_anchor: str


CASES = [
    CoverageCase(
        "structured",
        "tests/adversarial/structured_sequences.cpp",
        ("-DJMPXX_DIAGNOSTICS_ENABLED=1",
         "-DJMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE"),
    ),
    CoverageCase("differential", "tests/adversarial/differential_oracle.cpp"),
    CoverageCase("exception_safety", "tests/adversarial/exception_safety.cpp"),
    CoverageCase(
        "model_lifecycle",
        "tests/adversarial/model_lifecycle.cpp",
        ("-DJMPXX_DIAGNOSTICS_ENABLED=1",
         "-DJMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE"),
    ),
    CoverageCase(
        "usage_scenarios",
        "tests/adversarial/usage_scenarios.cpp",
        ("-DJMPXX_DIAGNOSTICS_ENABLED=0",
         "-DJMPXX_HARDENING_MODE=JMPXX_HARDENING_EXTENSIVE"),
        ("-fno-exceptions", "-fno-rtti"),
    ),
]

NARROW_CASES = [
    CoverageCase("exception_safety", "tests/adversarial/exception_safety.cpp"),
]

TRACKED_FILES = [
    "include/jmpxx/core/transport.hpp",
    "include/jmpxx/core/propagation.hpp",
    "include/jmpxx/diagnostics.hpp",
    "include/jmpxx/diagnostic/store.hpp",
    "include/jmpxx/erased.hpp",
    "include/jmpxx/interop/adapt.hpp",
    "include/jmpxx/interop/error_code.hpp",
    "include/jmpxx/interop/expected.hpp",
]

CRITICAL_REGIONS = [
    CriticalRegion("transport accessors", "include/jmpxx/core/transport.hpp",
                   "[[nodiscard]] constexpr bool has_value",
                   "// Pointer-like access"),
    CriticalRegion("transport cross-state assignment", "include/jmpxx/core/transport.hpp",
                   "void assign_from(const result& o)",
                   "void assign_from(result&& o)"),
    CriticalRegion("transport move cross-state assignment", "include/jmpxx/core/transport.hpp",
                   "void assign_from(result&& o)",
                   "};\n\n}  // namespace jmpxx"),
    CriticalRegion("propagation macros", "include/jmpxx/core/propagation.hpp",
                   "#define JMPXX_TRYV",
                   "namespace jmpxx {"),
    CriticalRegion("rich diagnostic policy", "include/jmpxx/diagnostics.hpp",
                   "class rich_error",
                   "namespace diagnostic {"),
    CriticalRegion("diagnostic store lifecycle", "include/jmpxx/diagnostic/store.hpp",
                   "class store",
                   "inline store& tls() noexcept"),
    CriticalRegion("type-erased policy dispatch", "include/jmpxx/erased.hpp",
                   "class erased_error",
                   "static_assert(__is_trivially_copyable(erased_error)"),
    CriticalRegion("error-code bridge", "include/jmpxx/interop/error_code.hpp",
                   "class jmpxx_category",
                   "}  // namespace jmpxx"),
    CriticalRegion("expected bridge", "include/jmpxx/interop/expected.hpp",
                   "template <class T, class E>",
                   "}  // namespace jmpxx"),
]


def run(cmd: list[str], timeout: int, env: dict[str, str] | None = None
        ) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, timeout=timeout,
                          env=env)


def line_of_anchor(path: Path, anchor: str) -> int:
    text = path.read_text()
    index = text.find(anchor)
    if index < 0:
        raise ValueError(f"anchor not found in {path}: {anchor!r}")
    return text[:index].count("\n") + 1


def build_and_run_cases(args: argparse.Namespace, work: Path) -> list[Path]:
    source_root = Path(args.source_root).resolve()
    cases = NARROW_CASES if args.mode == "inject" else CASES
    objects = []
    profiles = []
    for case in cases:
        exe = work / case.name
        cmd = [
            args.cxx, "-std=c++23", "-O0", "-g",
            "-fprofile-instr-generate", "-fcoverage-mapping",
            "-I", str(source_root / "include"),
            *case.definitions, *case.flags, str(source_root / case.source),
            "-o", str(exe),
        ]
        built = run(cmd, timeout=60)
        if built.returncode != 0:
            raise RuntimeError(built.stderr or built.stdout)
        profile = work / f"{case.name}.profraw"
        env = dict(**__import__("os").environ, LLVM_PROFILE_FILE=str(profile))
        executed = run([str(exe), *case.args], timeout=60, env=env)
        if executed.returncode != 0:
            raise RuntimeError(executed.stderr or executed.stdout)
        objects.append(exe)
        profiles.append(profile)

    merged = work / "merged.profdata"
    merged_cmd = [args.profdata, "merge", "-sparse", *map(str, profiles),
                  "-o", str(merged)]
    merged_proc = run(merged_cmd, timeout=60)
    if merged_proc.returncode != 0:
        raise RuntimeError(merged_proc.stderr or merged_proc.stdout)
    return [merged, *objects]


def export_coverage(args: argparse.Namespace, merged_and_objects: list[Path],
                    work: Path) -> dict:
    source_root = Path(args.source_root).resolve()
    merged = merged_and_objects[0]
    objects = merged_and_objects[1:]
    export_json = work / "coverage.json"
    cmd = [
        args.cov, "export", str(objects[0]), "-instr-profile", str(merged),
        *sum((["--object", str(obj)] for obj in objects[1:]), []),
        *sum((["--sources", str(source_root / rel)] for rel in TRACKED_FILES), []),
    ]
    exported = run(cmd, timeout=60)
    if exported.returncode != 0:
        raise RuntimeError(exported.stderr or exported.stdout)
    export_json.write_text(exported.stdout)
    return json.loads(exported.stdout)


def toolchain_has_expected(args: argparse.Namespace) -> bool:
    source_root = Path(args.source_root).resolve()
    cmd = [
        args.cxx, "-std=c++23", "-I", str(source_root / "include"),
        "-dM", "-E", "-x", "c++", "-include", "jmpxx/interop/expected.hpp",
        "/dev/null",
    ]
    probed = run(cmd, timeout=30)
    return probed.returncode == 0 and "#define JMPXX_INTEROP_HAS_EXPECTED 1" in probed.stdout


def matching_file(files: list[dict], rel: str) -> dict | None:
    suffix = "/" + rel
    for f in files:
        if f.get("filename", "").endswith(suffix):
            return f
    return None


def percent(covered: int, count: int) -> float:
    return 100.0 if count == 0 else (covered * 100.0 / count)


def branch_counts_in_region(file_data: dict, start: int, end: int) -> tuple[int, int]:
    total = 0
    covered = 0
    for branch in file_data.get("branches", []):
        if not branch or len(branch) < 6:
            continue
        line = int(branch[0])
        if start <= line <= end:
            total += 2
            if int(branch[4]) > 0:
                covered += 1
            if int(branch[5]) > 0:
                covered += 1
    return covered, total


def segment_reached_in_region(file_data: dict, start: int, end: int) -> bool:
    for segment in file_data.get("segments", []):
        if len(segment) >= 3 and start <= int(segment[0]) <= end and int(segment[2]) > 0:
            return True
    return False


def summarize(args: argparse.Namespace, data: dict) -> dict[str, object]:
    source_root = Path(args.source_root).resolve()
    files = data.get("data", [{}])[0].get("files", [])
    has_expected = toolchain_has_expected(args)
    tracked = []
    region_total = region_covered = 0
    branch_total = branch_covered = 0
    missing_files = []

    for rel in TRACKED_FILES:
        f = matching_file(files, rel)
        if not f:
            missing_files.append(rel)
            continue
        regions = f.get("summary", {}).get("regions", {})
        branches = f.get("summary", {}).get("branches", {})
        rc = int(regions.get("covered", 0))
        rt = int(regions.get("count", 0))
        bc = int(branches.get("covered", 0))
        bt = int(branches.get("count", 0))
        region_total += rt
        region_covered += rc
        branch_total += bt
        branch_covered += bc
        tracked.append({
            "file": rel,
            "regions": {"covered": rc, "count": rt, "percent": percent(rc, rt)},
            "branches": {"covered": bc, "count": bt, "percent": percent(bc, bt)},
        })

    critical = []
    missing_regions = []
    weak_branch_regions = []
    for region in CRITICAL_REGIONS:
        if region.name == "expected bridge" and not has_expected:
            critical.append({
                "name": region.name,
                "file": region.file,
                "skipped": "JMPXX_INTEROP_HAS_EXPECTED=0 on this coverage toolchain",
            })
            continue
        f = matching_file(files, region.file)
        start = line_of_anchor(source_root / region.file, region.start_anchor)
        end = line_of_anchor(source_root / region.file, region.end_anchor)
        if start > end:
            start, end = end, start
        reached = bool(f) and segment_reached_in_region(f, start, end)
        bc, bt = branch_counts_in_region(f or {}, start, end)
        branch_percent = percent(bc, bt)
        entry = {
            "name": region.name,
            "file": region.file,
            "start_line": start,
            "end_line": end,
            "region_reached": reached,
            "branches": {"covered": bc, "count": bt, "percent": branch_percent},
        }
        critical.append(entry)
        if not reached:
            missing_regions.append(region.name)
        if bt and branch_percent < args.critical_branch_floor:
            weak_branch_regions.append(region.name)

    region_percent = percent(region_covered, region_total)
    branch_percent = percent(branch_covered, branch_total)
    ok = (
        not missing_files and
        not missing_regions and
        not weak_branch_regions and
        region_percent >= args.region_floor and
        branch_percent >= args.branch_floor
    )
    if args.mode == "inject":
        ok = False if not ok else True
    return {
        "ok": ok,
        "mode": args.mode,
        "floors": {
            "region_percent": args.region_floor,
            "branch_percent": args.branch_floor,
            "critical_branch_percent": args.critical_branch_floor,
        },
        "totals": {
            "region_covered": region_covered,
            "region_count": region_total,
            "region_percent": region_percent,
            "branch_covered": branch_covered,
            "branch_count": branch_total,
            "branch_percent": branch_percent,
        },
        "missing_files": missing_files,
        "missing_regions": missing_regions,
        "weak_branch_regions": weak_branch_regions,
        "expected_bridge_available": has_expected,
        "tracked_files": tracked,
        "critical_regions": critical,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="jmpxx adversarial coverage floor")
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--profdata", required=True)
    parser.add_argument("--cov", required=True)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--build-root", required=True)
    parser.add_argument("--mode", choices=("clean", "inject"), default="clean")
    parser.add_argument("--print-config", action="store_true",
                        help="print the committed floors as JSON and exit, so the "
                             "documentation is checked against the floor this gate "
                             "enforces rather than against a copy of it")
    parser.add_argument("--region-floor", type=float, default=82.0)
    parser.add_argument("--branch-floor", type=float, default=78.0)
    parser.add_argument("--critical-branch-floor", type=float, default=50.0)
    parser.add_argument("--report")
    # The committed floors are readable without a build, so the documentation that
    # states them is checked against this gate's own configuration rather than
    # against a second copy of the numbers.
    if "--print-config" in sys.argv:
        defaults = parser.parse_args(["--cxx", "", "--profdata", "", "--cov", "",
                                      "--source-root", "", "--build-root", ""])
        print(json.dumps({"floors": {
            "region_percent": defaults.region_floor,
            "branch_percent": defaults.branch_floor,
            "critical_branch_percent": defaults.critical_branch_floor}}))
        return 0
    args = parser.parse_args()

    try:
        Path(args.build_root).mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="jmpxx_coverage_",
                                         dir=Path(args.build_root)) as tmp:
            work = Path(tmp)
            merged_and_objects = build_and_run_cases(args, work)
            data = export_coverage(args, merged_and_objects, work)
            report = summarize(args, data)
    except Exception as exc:
        report = {"ok": False, "mode": args.mode, "error": str(exc)}

    text = json.dumps(report, indent=2, sort_keys=True)
    if args.report:
        Path(args.report).write_text(text + "\n")
    # The headers this run actually measured, against the ones it tracks.
    print(f"    cases.asked  {len(report.get('tracked_files', []))}")
    print(f"    cases.known  {len(TRACKED_FILES)}")
    print(text)
    if report.get("ok"):
        print("adversarial coverage floor clean")
        return 0
    print("adversarial coverage floor failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
