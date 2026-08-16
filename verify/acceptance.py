#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the full acceptance sweep over a built tree and emit a structured report.

The sweep is the release gate: it runs every test tier and every gate in a build
directory through CTest, pairs each gate with its inverted self-test (the .teeth
cases, marked WILL_FAIL), and reports a gate as green only when the gate and its
inverted self-test both pass. It records the cell's identity and the headline
metrics from jmpxx-verify, so one invocation answers whether this cell is
releasable and a continuous-integration run consumes the result without parsing
prose.

A gate that has no passing inverted self-test is not trusted: the report marks it
unteethed and the verdict fails, enforcing that every gate has a negative check.
Run with --self-test to check that logic against a failed real test and a missing
inverted self-test.

Usage:
  acceptance.py --build-dir DIR [--format human|json] [--out FILE] [--cell NAME] [--jobs N]
  acceptance.py --self-test
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

# Each gate names the test prefixes that make it up. A test whose WILL_FAIL property
# is set is the gate's inverted self-test; the rest are the gate proper.
GATES = {
    "asm_golden":   ["codegen", "release_diff"],
    "size":         ["size_delta", "probe.size"],
    "no_alloc":     ["probe.alloc"],
    "compile_cost": ["compile_cost"],
    "perf":         ["bench", "unwind.benchmark"],
    "doc_claim":    ["doc_claim"],
    "abi_layout":   ["abi_layout"],
    "adversarial_fuzz": ["adversarial.fuzz"],
    "differential": ["adversarial.differential"],
    "exception_safety": ["adversarial.exception_safety"],
    "hardening": ["hardening"],
    "config_matrix": ["config_matrix"],
    "memory_sanitizer": ["msan"],
    "static_analysis": ["static_analysis"],
    "mutation_testing": ["mutation"],
    "adversarial_coverage": ["adversarial.coverage"],
    "model_lifecycle": ["model.lifecycle"],
    # The unwind arm's own gates. Each pairs a real run with an inverted case, so the
    # sweep reports the arm's depth evidence alongside the portable surface's.
    "unwind_link_time": ["unwind.lto"],
    "unwind_metadata": ["unwind.metadata"],
    "unwind_concurrency": ["unwind.concurrent"],
    "unwind_reentrancy": ["unwind.reentrancy"],
    "unwind_runtime_matrix": ["unwind.matrix"],
    "unwind_stress": ["unwind.stress"],
    "unwind_scaling": ["unwind.scale"],
}

# Metrics worth surfacing in the report, each the JSON of one jmpxx-verify probe.
METRIC_PROBES = ["platform", "size", "alloc", "destructors", "levels"]


def run_ctest_list(build_dir):
    """Return {test_name: will_fail_bool} from `ctest --show-only=json-v1`."""
    out = subprocess.run(
        ["ctest", "--test-dir", build_dir, "--show-only=json-v1"],
        capture_output=True, text=True, check=True).stdout
    data = json.loads(out)
    tests = {}
    for t in data.get("tests", []):
        will_fail = any(p.get("name") == "WILL_FAIL" and p.get("value") in (True, "TRUE", "ON")
                        for p in t.get("properties", []))
        tests[t["name"]] = will_fail
    return tests


def run_ctest(build_dir, jobs):
    """Run the suite, returning (ctest_exit, {test_name: passed_bool})."""
    with tempfile.TemporaryDirectory() as tmp:
        junit = os.path.join(tmp, "junit.xml")
        proc = subprocess.run(
            ["ctest", "--test-dir", build_dir, "-j", str(jobs),
             "--output-on-failure", "--output-junit", junit],
            capture_output=True, text=True)
        results = {}
        if os.path.exists(junit):
            for case in ET.parse(junit).getroot().iter("testcase"):
                failed = (case.get("status") in ("fail", "failed") or
                          case.find("failure") is not None or
                          case.find("error") is not None)
                results[case.get("name")] = not failed
        return proc.returncode, results


def probe_json(verify_bin, probe):
    """Run one jmpxx-verify probe in JSON mode and return its parsed object."""
    try:
        out = subprocess.run([verify_bin, probe, "--format=json"],
                             capture_output=True, text=True).stdout.strip()
        return json.loads(out.splitlines()[-1]) if out else None
    except (OSError, json.JSONDecodeError):
        return None


def classify(tests, results):
    """Group results into gates and tiers, including inverted self-tests."""
    gate_of = {}
    for gate, prefixes in GATES.items():
        for name in tests:
            if any(name == p or name.startswith(p + ".") for p in prefixes):
                gate_of[name] = gate

    gates = {}
    for gate, prefixes in GATES.items():
        members = [n for n in tests if gate_of.get(n) == gate]
        if not members:
            continue
        real = [n for n in members if not tests[n]]
        teeth = [n for n in members if tests[n]]
        real_ok = all(results.get(n, False) for n in real)
        teeth_ok = bool(teeth) and all(results.get(n, False) for n in teeth)
        status = ("green_with_teeth" if real_ok and teeth_ok
                  else "unteethed" if real_ok and not teeth
                  else "fail")
        gates[gate] = {"status": status, "real": sorted(real),
                       "teeth": sorted(teeth), "real_passed": real_ok,
                       "teeth_passed": teeth_ok}

    tiers = {n: results.get(n, False) for n in tests if n not in gate_of}
    return gates, tiers


def build_report(tests, results, ctest_exit, cell, metrics):
    gates, tiers = classify(tests, results)
    total = len(tests)
    passed = sum(1 for n in tests if results.get(n, False))
    gates_ok = all(g["status"] == "green_with_teeth" for g in gates.values())
    verdict = (ctest_exit == 0 and passed == total and gates_ok)
    return {
        "tool": "jmpxx-acceptance",
        "schema": 1,
        "cell": cell,
        "summary": {"tests_total": total, "tests_passed": passed,
                    "ctest_exit": ctest_exit, "gates_green_with_teeth": gates_ok},
        "gates": gates,
        "tiers": tiers,
        "metrics": metrics,
        "verdict": "pass" if verdict else "fail",
    }


def render_human(rep):
    out = []
    c = rep["cell"]
    out.append(f"acceptance sweep: {c.get('compiler','?')} {c.get('arch','?')}/"
               f"{c.get('os','?')} c++{c.get('cpp_standard','?')}")
    s = rep["summary"]
    out.append(f"  tests:  {s['tests_passed']}/{s['tests_total']} passed "
               f"(ctest exit {s['ctest_exit']})")
    out.append("  gates:")
    for name, g in sorted(rep["gates"].items()):
        mark = {"green_with_teeth": "OK ", "unteethed": "NOTEETH", "fail": "FAIL"}[g["status"]]
        out.append(f"    [{mark}] {name}: {len(g['real'])} gate + "
                   f"{len(g['teeth'])} inverted")
    failed = [n for n, ok in rep["tiers"].items() if not ok]
    if failed:
        out.append("  failed tiers: " + ", ".join(sorted(failed)))
    out.append(f"  VERDICT: {rep['verdict'].upper()}")
    return "\n".join(out)


def self_test():
    """Prove the verdict logic fails a failed real test and a gate with no
    passing inverted self-test."""
    base_tests = {"size_delta": False, "size_delta.teeth": True, "probe.size": False}
    base_results = {"size_delta": True, "size_delta.teeth": True, "probe.size": True}
    ok = build_report(base_tests, base_results, 0, {}, {})
    assert ok["verdict"] == "pass", "a fully green cell must pass"

    broken = dict(base_results); broken["size_delta"] = False
    assert build_report(base_tests, broken, 8, {}, {})["verdict"] == "fail", \
        "a failed real test must fail the verdict"

    no_teeth_tests = {"size_delta": False, "probe.size": False}
    no_teeth_res = {"size_delta": True, "probe.size": True}
    r = build_report(no_teeth_tests, no_teeth_res, 0, {}, {})
    assert r["gates"]["size"]["status"] == "unteethed", \
        "a gate with no inverted self-test is unteethed"
    assert r["verdict"] == "fail", "an unteethed gate must fail the verdict"

    failed_teeth = dict(base_results); failed_teeth["size_delta.teeth"] = False
    assert build_report(base_tests, failed_teeth, 8, {}, {})["verdict"] == "fail", \
        "a failed inverted self-test must fail the verdict"
    print("acceptance self-test: PASS "
          "(verdict logic catches failed tests and missing inverted self-tests)")
    return 0


def main():
    ap = argparse.ArgumentParser(description="jmpxx acceptance sweep")
    ap.add_argument("--build-dir")
    ap.add_argument("--format", choices=["human", "json"], default="human")
    ap.add_argument("--out")
    ap.add_argument("--cell", default=None)
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.build_dir:
        ap.error("--build-dir is required (or use --self-test)")

    tests = run_ctest_list(args.build_dir)
    ctest_exit, results = run_ctest(args.build_dir, args.jobs)

    verify_bin = os.path.join(args.build_dir, "verify", "jmpxx-verify")
    metrics = {}
    cell = {}
    if os.path.exists(verify_bin):
        plat = probe_json(verify_bin, "platform")
        if plat:
            cell = plat.get("metrics", {})
        for probe in METRIC_PROBES:
            obj = probe_json(verify_bin, probe)
            if obj:
                metrics[probe] = obj.get("metrics", {})
    if args.cell:
        cell["name"] = args.cell

    report = build_report(tests, results, ctest_exit, cell, metrics)
    text = (json.dumps(report, indent=2) if args.format == "json"
            else render_human(report))
    if args.out:
        with open(args.out, "w") as f:
            f.write(text + "\n")
    print(text)
    return 0 if report["verdict"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
