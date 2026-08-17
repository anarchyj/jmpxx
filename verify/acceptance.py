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
import re
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
    # The claim-integrity gates: what the public surface says about itself, held to the
    # same rule as what the code does.
    "claim_backing": ["claim.audit"],
    "claim_provenance": ["claim.provenance"],
    "claim_home": ["claim.canonical_home"],
    "foreign_headers": ["interop.foreign_headers"],
    "gate_accounting": ["gate.accounting"],
    "unwind_body_frame": ["unwind.body_frame"],
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

# What each gate must report it checked, summed over the tests that make it up. A gate
# reporting a pass is not a gate having asked its question: a probe once fell from ten
# cases to one while every report still said pass, so each gate prints how many cases
# it asked and how many it knows about, and the number it must reach is recorded here
# rather than only inside the gate. Trimming a gate and its own expectation together
# still fails, because this figure does not move with them.
GATE_CASES = {
    "abi_layout": 8,
    "adversarial_coverage": 8,
    "adversarial_fuzz": 512,
    "asm_golden": 7,
    "compile_cost": 2,
    "config_matrix": 8,
    "differential": 4096,
    "doc_claim": 27,
    "exception_safety": 7,
    "foreign_headers": 4,
    "gate_accounting": 30,
    "hardening": 10,
    "memory_sanitizer": 1,
    "model_lifecycle": 4,
    "mutation_testing": 21,
    "no_alloc": 3,
    "perf": 12,
    "size": 7,
    "static_analysis": 1,
    "unwind_body_frame": 8,
    "unwind_concurrency": 16,
    "unwind_link_time": 5,
    "unwind_metadata": 2,
    "unwind_reentrancy": 7,
    "unwind_runtime_matrix": 10,
    "unwind_scaling": 4,
    "unwind_stress": 12,
    # The claim gates read every claim on the public surface, so their case count is the
    # size of that surface and moves whenever the documentation is edited. A floor is
    # what carries the property here: the audit may cover more than this and may not
    # cover less, so a surface that quietly stops being read still fails.
    "claim_backing": {"at_least": 850},
    "claim_provenance": {"at_least": 850},
    "claim_home": {"at_least": 850},
}

# A gate that cannot carry a case count, with the reason. Being named here is the only
# alternative to reporting, so a gate cannot go quiet by omission.
ACCOUNTING_EXEMPT = {}

# A defence that is real and whose subject could not be made to fail. The rule is teeth,
# removal, or this, and this state exists so that the honest answer is also the cheap
# one: an author facing a defence with no failing subject records what they tried
# instead of deleting something real or manufacturing a tooth. Each entry names the
# attempts, and the gate re-runs them, so a compiler that starts breaking the defence
# moves it out of this state rather than leaving it here unexamined.
UNFALSIFIABLE_DEFENCES = {
    "unwind_body_frame":
        "The trampoline that keeps the escape body's objects out of the landing frame. "
        "The attempt: a copy of the include tree in which the trampoline is forced "
        "inline, which is exactly the shape the defence exists to prevent. Run at -O0 "
        "through -O3 on GCC 13.3.0 and Clang 18.1.3 on 2026-08-17, the escape still "
        "destroyed every object it constructed, so no known-bad subject exists. The "
        "gate re-runs the attempt on every sweep and reports if it ever reproduces.",
}

# The share of the gate set that must carry case accounting. It is recorded so the
# property cannot be met by exempting gates until the ones that remain all comply.
ACCOUNTING_FLOOR = 1.0

# A gate whose result depends on how the sweep was invoked declares the condition, and
# the sweep honors it. A latency measurement taken while the rest of the sweep loads
# the machine reports the load, which is a gate failing for a reason unrelated to what
# it measures.
SERIAL_GATES = {
    "unwind.scale": "measures per-escape latency as threads are added, so it needs the "
                    "machine to itself",
    "unwind.scale.teeth": "the same measurement with serialization injected",
    "unwind.benchmark": "measures the escape's tail against a throw's in the same loop",
    "unwind.benchmark.teeth": "the same measurement with jitter injected",
    "bench.gate": "co-measures jmpxx against the hand-written baseline",
    "bench.gate.teeth": "the same measurement with the slowed kernel",
}

CASE_LINE = re.compile(r"cases\.(asked|known)\D+(\d+)")


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
    """Run the suite, returning (ctest_exit, {name: passed}, {name: (asked, known)})."""
    with tempfile.TemporaryDirectory() as tmp:
        junit = os.path.join(tmp, "junit.xml")
        proc = subprocess.run(
            ["ctest", "--test-dir", build_dir, "-j", str(jobs),
             "--output-on-failure", "--output-junit", junit],
            capture_output=True, text=True)
        results, cases = {}, {}
        if os.path.exists(junit):
            for case in ET.parse(junit).getroot().iter("testcase"):
                failed = (case.get("status") in ("fail", "failed") or
                          case.find("failure") is not None or
                          case.find("error") is not None)
                results[case.get("name")] = not failed
                counts = read_cases(case)
                if counts:
                    cases[case.get("name")] = counts
        return proc.returncode, results, cases


def read_cases(testcase):
    """The case counts a test printed, or None. A gate states how many cases it asked
    and how many it knows of; the two disagreeing is the gate reporting that it went
    quiet."""
    out = testcase.find("system-out")
    if out is None or not out.text:
        return None
    found = {}
    for kind, number in CASE_LINE.findall(out.text):
        found[kind] = found.get(kind, 0) + int(number)
    if "asked" not in found or "known" not in found:
        return None
    return (found["asked"], found["known"])


def account(gates, cases):
    """Hold each gate's reported case count against the count recorded for it."""
    accounting, findings = {}, []
    for name, gate in gates.items():
        # Only the gate proper is counted. An inverted self-test deliberately changes
        # the subject, often by adding or removing a case, so folding its count in
        # would make the expectation move whenever a tooth is sharpened.
        members = gate["real"]
        reported = [cases[m] for m in members if m in cases]
        asked = sum(a for a, _ in reported)
        known = sum(k for _, k in reported)
        expected = GATE_CASES.get(name)
        floor = expected["at_least"] if isinstance(expected, dict) else None
        entry = {"tests_reporting": len(reported), "tests": len(members),
                 "asked": asked, "known": known, "expected": expected,
                 "exempt": ACCOUNTING_EXEMPT.get(name, "")}
        accounting[name] = entry
        if entry["exempt"]:
            continue
        if expected is None:
            findings.append(f"{name}: no recorded case count and no reason it cannot "
                            f"carry one")
        elif not reported:
            findings.append(f"{name}: reports no case count; a gate that does not say "
                            f"what it checked cannot be read as evidence")
        elif asked != known:
            findings.append(f"{name}: asked {asked} cases and knows of {known}")
        elif floor is not None and known < floor:
            findings.append(f"{name}: reports {known} cases, below the {floor} recorded "
                            f"as its floor")
        elif floor is None and known != expected:
            findings.append(f"{name}: reports {known} cases against the {expected} "
                            f"recorded for it")
    covered = sum(1 for n, e in accounting.items()
                  if not e["exempt"] and e["tests_reporting"])
    total = len(accounting)
    share = covered / total if total else 1.0
    if share < ACCOUNTING_FLOOR:
        findings.append(f"{covered} of {total} gates carry case accounting, below the "
                        f"recorded {ACCOUNTING_FLOOR:.0%}")
    return {"per_gate": accounting, "covered": covered, "total": total,
            "share": round(share, 3), "floor": ACCOUNTING_FLOOR,
            "findings": findings}


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
        unfalsifiable = UNFALSIFIABLE_DEFENCES.get(gate, "")
        status = ("green_with_teeth" if real_ok and teeth_ok
                  else "unfalsifiable_defence" if real_ok and not teeth and unfalsifiable
                  else "unteethed" if real_ok and not teeth
                  else "fail")
        gates[gate] = {"status": status, "real": sorted(real),
                       "teeth": sorted(teeth), "real_passed": real_ok,
                       "teeth_passed": teeth_ok}
        if unfalsifiable:
            gates[gate]["falsification_attempts"] = unfalsifiable

    tiers = {n: results.get(n, False) for n in tests if n not in gate_of}
    return gates, tiers


def build_report(tests, results, ctest_exit, cell, metrics, cases=None):
    gates, tiers = classify(tests, results)
    total = len(tests)
    passed = sum(1 for n in tests if results.get(n, False))
    gates_ok = all(g["status"] in ("green_with_teeth", "unfalsifiable_defence")
                   for g in gates.values())
    accounting = account(gates, cases or {})
    verdict = (ctest_exit == 0 and passed == total and gates_ok
               and not accounting["findings"])
    return {
        "tool": "jmpxx-acceptance",
        "schema": 1,
        "cell": cell,
        "summary": {"tests_total": total, "tests_passed": passed,
                    "ctest_exit": ctest_exit, "gates_green_with_teeth": gates_ok,
                    "gates_with_case_accounting": accounting["share"]},
        "gates": gates,
        "case_accounting": accounting,
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
        mark = {"green_with_teeth": "OK ", "unteethed": "NOTEETH", "fail": "FAIL",
                "unfalsifiable_defence": "NOFALSIFY"}[g["status"]]
        out.append(f"    [{mark}] {name}: {len(g['real'])} gate + "
                   f"{len(g['teeth'])} inverted")
    acc = rep["case_accounting"]
    out.append(f"  case accounting: {acc['covered']}/{acc['total']} gates report what "
               f"they checked (floor {acc['floor']:.0%})")
    for why in acc["findings"]:
        out.append(f"    ACCOUNTING {why}")
    failed = [n for n, ok in rep["tiers"].items() if not ok]
    if failed:
        out.append("  failed tiers: " + ", ".join(sorted(failed)))
    out.append(f"  VERDICT: {rep['verdict'].upper()}")
    return "\n".join(out)


def self_test():
    """Prove the verdict logic fails a failed real test, a gate with no passing
    inverted self-test, and a gate that stopped saying what it checked."""
    base_tests = {"size_delta": False, "size_delta.teeth": True, "probe.size": False}
    base_results = {"size_delta": True, "size_delta.teeth": True, "probe.size": True}
    whole = GATE_CASES["size"]
    base_cases = {"size_delta": (whole - 5, whole - 5), "probe.size": (5, 5)}
    ok = build_report(base_tests, base_results, 0, {}, {}, base_cases)
    assert ok["verdict"] == "pass", "a fully green cell must pass"

    broken = dict(base_results); broken["size_delta"] = False
    assert build_report(base_tests, broken, 8, {}, {}, base_cases)["verdict"] == "fail", \
        "a failed real test must fail the verdict"

    no_teeth_tests = {"size_delta": False, "probe.size": False}
    no_teeth_res = {"size_delta": True, "probe.size": True}
    r = build_report(no_teeth_tests, no_teeth_res, 0, {}, {}, base_cases)
    assert r["gates"]["size"]["status"] == "unteethed", \
        "a gate with no inverted self-test is unteethed"
    assert r["verdict"] == "fail", "an unteethed gate must fail the verdict"

    # The third disposition is not a way around the teeth rule. It reaches only the
    # gates recorded as unfalsifiable defences with the attempts that failed, and an
    # ordinary gate that simply has no inverted case is still unteethed.
    assert "size" not in UNFALSIFIABLE_DEFENCES, \
        "the third disposition must not cover an ordinary gate"

    failed_teeth = dict(base_results); failed_teeth["size_delta.teeth"] = False
    assert build_report(base_tests, failed_teeth, 8, {}, {}, base_cases)["verdict"] == "fail", \
        "a failed inverted self-test must fail the verdict"

    # A gate that goes quiet: every test still passes, and the case count it reports
    # has fallen below the count recorded for it. This is the failure the whole
    # accounting layer exists for, and it must fail the verdict.
    quiet = dict(base_cases); quiet["probe.size"] = (1, 1)
    r = build_report(base_tests, base_results, 0, {}, {}, quiet)
    assert r["verdict"] == "fail", "a gate reporting fewer cases must fail the verdict"
    assert any("recorded for it" in f for f in r["case_accounting"]["findings"]), \
        "the shortfall must be reported against the recorded count"

    silent = build_report(base_tests, base_results, 0, {}, {}, {})
    assert silent["verdict"] == "fail", \
        "a gate that reports no case count at all must fail the verdict"

    # And a gate whose two counts disagree: it asked fewer questions than it knows of.
    disagree = dict(base_cases); disagree["probe.size"] = (1, 5)
    r = build_report(base_tests, base_results, 0, {}, {}, disagree)
    assert r["verdict"] == "fail", "asked and known disagreeing must fail the verdict"

    print("acceptance self-test: PASS (the verdict logic catches a failed test, a "
          "missing inverted self-test, and a gate that stopped asking its question)")
    return 0


def check_accounting(build_dir, inject):
    """The static half of the case-accounting property, checkable without running the
    suite: every gate has a recorded case count or a reason it cannot carry one, the
    share carrying accounting is at or above the recorded floor, and every gate that
    declares it needs a quiet machine is registered to run alone. The dynamic half,
    holding each gate's reported count against the recorded one, is the sweep itself.

    The inverted run drops one gate's recorded count, which must fail: a gate set that
    can lose an expectation without going red is a gate set that can go quiet."""
    recorded = dict(GATE_CASES)
    if inject:
        recorded.pop(sorted(recorded)[0], None)
    findings = []
    for gate in GATES:
        if gate not in recorded and gate not in ACCOUNTING_EXEMPT:
            findings.append(f"{gate}: no recorded case count and no reason it cannot "
                            f"carry one")
    covered = sum(1 for g in GATES if g in recorded)
    share = covered / len(GATES) if GATES else 1.0
    if share < ACCOUNTING_FLOOR:
        findings.append(f"{covered} of {len(GATES)} gates carry a recorded case count, "
                        f"below the recorded {ACCOUNTING_FLOOR:.0%}")

    # A timing gate run beside the rest of the sweep reports the machine's load. Each
    # one declares that, and the build must register it to run alone.
    serial = serial_tests(build_dir)
    for test, why in SERIAL_GATES.items():
        if test in serial:
            continue
        if test not in known_tests(build_dir):
            continue
        findings.append(f"{test} declares it needs a quiet machine ({why}) and is not "
                        f"registered to run alone")

    print(f"gate case accounting: {covered}/{len(GATES)} gates carry a recorded count "
          f"(floor {ACCOUNTING_FLOOR:.0%})")
    print(f"    cases.asked  {covered}")
    print(f"    cases.known  {len(GATES)}")
    print(f"    gates declaring a quiet machine: {len(SERIAL_GATES)}, "
          f"{sum(1 for t in SERIAL_GATES if t in serial)} registered to run alone")
    for why in findings:
        print(f"  FAIL {why}")
    print("  VERDICT: " + ("PASS" if not findings else "FAIL"))
    return 0 if not findings else 1


def _test_list(build_dir):
    out = subprocess.run(["ctest", "--test-dir", build_dir, "--show-only=json-v1"],
                         capture_output=True, text=True, check=True).stdout
    return json.loads(out).get("tests", [])


def known_tests(build_dir):
    return {t["name"] for t in _test_list(build_dir)}


def serial_tests(build_dir):
    """Tests the build registers to run alone."""
    names = set()
    for t in _test_list(build_dir):
        for prop in t.get("properties", []):
            if prop.get("name") == "RUN_SERIAL" and prop.get("value") in (True, "TRUE", "ON"):
                names.add(t["name"])
    return names


def main():
    ap = argparse.ArgumentParser(description="jmpxx acceptance sweep")
    ap.add_argument("--build-dir")
    ap.add_argument("--format", choices=["human", "json"], default="human")
    ap.add_argument("--out")
    ap.add_argument("--cell", default=None)
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--check-accounting", action="store_true",
                    help="check the gate set's case-accounting registry without "
                         "running the suite")
    ap.add_argument("--inject", action="store_true",
                    help="drop a gate's recorded case count, which must fail")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if args.check_accounting:
        if not args.build_dir:
            ap.error("--check-accounting needs --build-dir")
        return check_accounting(args.build_dir, args.inject)
    if not args.build_dir:
        ap.error("--build-dir is required (or use --self-test)")

    tests = run_ctest_list(args.build_dir)
    ctest_exit, results, cases = run_ctest(args.build_dir, args.jobs)

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

    report = build_report(tests, results, ctest_exit, cell, metrics, cases)
    text = (json.dumps(report, indent=2) if args.format == "json"
            else render_human(report))
    if args.out:
        with open(args.out, "w") as f:
            f.write(text + "\n")
    print(text)
    return 0 if report["verdict"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
