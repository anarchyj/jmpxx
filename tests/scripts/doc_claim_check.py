#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Hold every documented value against the value the verification surface reports.

The rule this enforces is that a number in the public documentation is a measurement,
not a sentence. Each entry in the committed value map names where the value comes
from, the wording it appears in, and the documents that state it. The gate re-derives
the value and requires the documents to still say it, so a measurement that moves
without the prose moving fails the build.

The covered set is part of the report. A gate that quietly checks fewer values than
it did is the same defect as a gate that quietly checks fewer cases, so the map
records how many values must be covered and a shortfall fails.

Usage:
  doc_claim_check.py --source-root R --build-dir D --values F [--report J]
                     [--overlay DOC=PATH] [--only-source KIND]
"""
import argparse
import json
import os
import re
import subprocess
import sys

ONES = ["zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
        "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
        "sixteen", "seventeen", "eighteen", "nineteen"]
TENS = ["", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty",
        "ninety"]


def in_words(n):
    """English for the small counts the documentation spells out. Above a thousand the
    documentation uses digits, so the digit form is the only rendering offered."""
    if n < 0:
        return "minus " + in_words(-n)
    if n < 20:
        return ONES[n]
    if n < 100:
        return TENS[n // 10] + ("-" + ONES[n % 10] if n % 10 else "")
    if n < 1000 and n % 100 == 0:
        return ONES[n // 100] + " hundred"
    if n < 1000:
        return ONES[n // 100] + " hundred and " + in_words(n % 100)
    return str(n)


def render(form, value):
    """One wording of a value: {digits} as the number, {grouped} with thousands
    separators, {words} as English, {value} as the raw string a non-numeric metric
    carries."""
    digits, words = str(value), str(value)
    if isinstance(value, bool):
        digits = words = "true" if value else "false"
    elif isinstance(value, (int, float)) and float(value).is_integer():
        digits, words = str(int(value)), in_words(int(value))
    elif isinstance(value, float):
        digits = words = ("%g" % value)
    grouped = f"{int(value):,}" if isinstance(value, (int, float)) and not isinstance(
        value, bool) and float(value).is_integer() else digits
    return (form.replace("{digits}", digits).replace("{words}", words)
                .replace("{grouped}", grouped).replace("{value}", str(value)))


def probe(build_dir, name, cache):
    """One jmpxx-verify probe's metrics, run once per sweep."""
    if name in cache:
        return cache[name]
    binary = os.path.join(build_dir, "verify", "jmpxx-verify")
    out = subprocess.run([binary, name, "--format=json"], capture_output=True,
                         text=True).stdout.strip()
    metrics = {}
    if out:
        metrics = json.loads(out.splitlines()[-1]).get("metrics", {})
    cache[name] = metrics
    return metrics


def probe_with_args(build_dir, argv, cache):
    """A probe invoked with its own arguments, cached by the whole invocation."""
    key = " ".join(argv)
    if key in cache:
        return cache[key]
    binary = os.path.join(build_dir, "verify", "jmpxx-verify")
    out = subprocess.run([binary] + argv + ["--format=json"], capture_output=True,
                         text=True).stdout.strip()
    cache[key] = json.loads(out.splitlines()[-1]).get("metrics", {}) if out else {}
    return cache[key]


def report_file(build_dir, path, cache):
    """A gate's own JSON report, so a documented floor is held against the floor the
    gate enforced in this build rather than against a second copy of the number."""
    full = os.path.join(build_dir, path)
    if full not in cache:
        with open(full, encoding="utf-8") as f:
            cache[full] = json.load(f)
    return cache[full]


def dotted(obj, key):
    for part in key.split("."):
        if isinstance(obj, list):
            obj = obj[int(part)]
        else:
            obj = obj[part]
    return obj


def measure(entry, build_dir, source_root, caches):
    """The current value of one entry, or None with the reason it is unavailable."""
    source = (entry["source"].replace("{source_root}", source_root)
                             .replace("{build_dir}", build_dir))
    kind, _, rest = source.partition(":")
    if kind == "verify":
        metrics = probe(build_dir, rest, caches["probe"])
        if entry["key"] not in metrics:
            return None, f"jmpxx-verify {rest} reports no {entry['key']}"
        return metrics[entry["key"]], None
    if kind == "verify-args":
        # A probe that needs its subject named, such as compile-cost with the fixture
        # and baseline the comparison quotes.
        metrics = probe_with_args(build_dir, rest.split(), caches["probe"])
        if entry["key"] not in metrics:
            return None, f"jmpxx-verify {rest} reports no {entry['key']}"
        return metrics[entry["key"]], None
    if kind == "report":
        try:
            return dotted(report_file(build_dir, rest, caches["report"]),
                          entry["key"]), None
        except (OSError, KeyError, IndexError, ValueError) as exc:
            return None, f"{rest} is not readable here: {exc}"
    if kind == "tool":
        # A gate's own committed configuration, so a documented floor is held against
        # the floor that gate enforces rather than against a second copy of it.
        if rest not in caches["tool"]:
            out = subprocess.run(rest.split(), capture_output=True, text=True).stdout
            try:
                caches["tool"][rest] = json.loads(out)
            except json.JSONDecodeError:
                return None, f"{rest} printed no configuration"
        try:
            return dotted(caches["tool"][rest], entry["key"]), None
        except (KeyError, IndexError, ValueError) as exc:
            return None, f"{rest} has no {entry['key']}: {exc}"
    if kind == "file":
        path, _, pattern = rest.partition("::")
        try:
            with open(os.path.join(source_root, path), encoding="utf-8") as f:
                m = re.search(pattern, f.read(), re.M)
        except OSError as exc:
            return None, str(exc)
        if not m:
            return None, f"{path} does not state {pattern}"
        return m.group(1), None
    return None, f"unknown source kind {kind}"


def document_text(source_root, doc, overlay):
    path = overlay.get(doc, os.path.join(source_root, doc))
    with open(path, encoding="utf-8") as f:
        return " ".join(f.read().split())


def check(values, build_dir, source_root, overlay, only_source):
    caches = {"probe": {}, "report": {}, "tool": {}}
    covered, findings, skipped = [], [], []
    for entry in values["values"]:
        if only_source and not entry["source"].startswith(only_source):
            continue
        value, why = measure(entry, build_dir, source_root, caches)
        if value is None:
            skipped.append({"id": entry["id"], "why": why})
            findings.append({"id": entry["id"], "why": "unavailable: " + why})
            continue
        if entry.get("scale"):
            value = value * entry["scale"]
        # A value the platform contributes rather than the library, such as what a
        # throw costs in the system unwinder, moves with the toolchain. Those entries
        # state the figure the documentation rounds to and the band it must stay
        # inside, so the gate fails on a real change and not on a rebuild.
        stated_value = value
        if "documented" in entry:
            stated_value = entry["documented"]
            band = abs(entry["documented"]) * entry.get("tolerance_percent", 0) / 100.0
            if abs(value - entry["documented"]) > band:
                findings.append({
                    "id": entry["id"], "measured": value,
                    "why": f"measured {value}, outside {entry.get('tolerance_percent', 0)}% "
                           f"of the documented {entry['documented']}"})
        forms = [render(f, stated_value) for f in entry["forms"]]
        stated = True
        for doc in entry["documents"]:
            text = document_text(source_root, doc, overlay)
            if not any(f.lower() in text.lower() for f in forms):
                stated = False
                findings.append({
                    "id": entry["id"], "document": doc, "measured": value,
                    "why": f"{doc} does not state the measured value; expected one of "
                           + " | ".join(forms)})
        covered.append({"id": entry["id"], "measured": value,
                        "documents": entry["documents"], "stated": stated})
    return covered, findings, skipped


def main():
    ap = argparse.ArgumentParser(description="jmpxx documented-value currency")
    ap.add_argument("--source-root", required=True)
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--values", required=True)
    ap.add_argument("--report")
    ap.add_argument("--overlay", action="append", default=[],
                    help="DOC=PATH, read DOC from PATH instead (the inverted case)")
    ap.add_argument("--only-source", help="cover only entries from this source kind")
    args = ap.parse_args()

    with open(args.values, encoding="utf-8") as f:
        values = json.load(f)
    overlay = dict(o.split("=", 1) for o in args.overlay)

    covered, findings, skipped = check(values, args.build_dir, args.source_root,
                                       overlay, args.only_source)
    floor_key = "covered_floor_" + (args.only_source or "verify")
    floor = values.get(floor_key, 0)
    if len(covered) < floor:
        findings.append({"id": "coverage", "why":
                         f"{len(covered)} values covered, below the recorded floor "
                         f"{floor}; the gate cannot be narrowed to pass"})

    applicable = [e for e in values["values"]
                  if not args.only_source or e["source"].startswith(args.only_source)]
    rep = {"tool": "jmpxx-doc-claim", "schema": 1,
           "cases": {"asked": len(covered), "known": len(applicable)},
           "floor": floor,
           "covered": covered, "skipped": skipped, "findings": findings,
           "ok": not findings}
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            json.dump(rep, f, indent=1)
    print(f"documented-value currency: {len(covered)} values checked against the "
          f"harness (floor {floor})")
    print(f"    cases.asked  {len(covered)}")
    print(f"    cases.known  {len(applicable)}")
    for c in covered:
        print(f"    {c['id']:34s} {str(c['measured'])[:40]}")
    for f in findings:
        print(f"  FAIL {f['id']}: {f['why']}")
    print("  VERDICT: " + ("PASS" if rep["ok"] else "FAIL"))
    return 0 if rep["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
