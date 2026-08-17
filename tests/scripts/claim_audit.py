#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Audit every claim on jmpxx's public surface against the artifact that backs it.

The project's rule is that no claim about cost, size, or behaviour ships without a
gate behind it. This applies that rule to the sentences the project writes about
itself. Every claim-bearing sentence in the public prose and the public code comments
carries one recorded disposition in the ledger:

  gate         a live gate or test tier proves it, named by its CTest test name
  artifact     a committed file shows it, named by its path
  measurement  it came from a measurement, recorded with the toolchain, target and
               date it was obtained on, so it can be re-checked and can expire
  rationale    it states a design judgement rather than a checkable property, which a
               claim carrying a measured quantity may not do
  withdrawn    it was found wrong and removed, and its wording must not come back

A claim whose wording changes gets a new identity and is undispositioned until it is
entered again, which is what stops a corrected sentence from drifting back.

Usage:
  claim_audit.py --source-root R --ledger F --build-dir D [--mode audit|provenance]
                 [--report J]
  claim_audit.py --source-root R --ledger F --build-dir D --inject KIND
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import claim_surface as surface  # noqa: E402

DISPOSITIONS = ("gate", "artifact", "measurement", "rationale", "withdrawn")
PROVENANCE_FIELDS = ("tool", "target", "obtained")
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")

INJECTIONS = {
    "unbacked-claim":
        "a fabricated performance claim added to a public document, which no gate, "
        "artifact or measurement backs",
    "withdrawn-form":
        "a withdrawn claim's original wording put back into a public document",
    "stripped-provenance":
        "a measured claim with its toolchain, target and date removed",
    "reclassified":
        "a measured claim re-dispositioned as a design judgement to dodge provenance",
}
FABRICATED = ("Propagating a failure through jmpxx costs 3 nanoseconds on every "
              "supported target and never touches memory.")


def load_ledger(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def registered_tests(build_dir):
    """Every CTest test name in a built tree. A gate disposition must name one of
    these, so a claim cannot point at a test that was renamed or removed."""
    out = subprocess.run(["ctest", "--test-dir", build_dir, "--show-only=json-v1"],
                         capture_output=True, text=True, check=True).stdout
    return {t["name"] for t in json.loads(out).get("tests", [])}


def read_surface(root, overlay):
    """The public surface, with any overlaid file read from its replacement."""
    files = surface.surface_files(root)
    if not overlay:
        return surface.claims(root, files), files
    found = []
    for rel, kind in files:
        path = overlay.get(rel, os.path.join(root, rel))
        with open(path, encoding="utf-8") as f:
            text = f.read()
        sentences = (surface._prose_sentences(text) if kind == "prose"
                     else surface._code_sentences(text))
        for line, sentence in sentences:
            if surface.is_claim(sentence):
                found.append({"file": rel, "line": line, "text": sentence,
                              "digest": surface.digest(sentence), "kind": kind})
    return found, files


MARKUP = re.compile(r"(//+|[*`])")


def surface_text(root, files, overlay):
    """The raw text of every public file, flattened so a withdrawn wording is found
    wherever it reappears: inside an example block the sentence reader skips, wrapped
    across comment lines, or restyled with different emphasis."""
    blob = []
    for rel, _ in files:
        path = overlay.get(rel, os.path.join(root, rel)) if overlay else os.path.join(root, rel)
        with open(path, encoding="utf-8") as f:
            blob.append(" ".join(MARKUP.sub(" ", f.read()).split()).lower())
    return blob


def flatten(text):
    """A claim's wording in the same flattened form the surface is searched in."""
    return " ".join(MARKUP.sub(" ", text).split()).lower()


def audit(root, ledger, tests, overlay=None):
    """Check every claim against the ledger and every ledger entry against reality."""
    claims, files = read_surface(root, overlay)
    entries = {(e["file"], e["digest"]): e for e in ledger["claims"]}
    findings = []
    seen = set()

    for claim in claims:
        key = (claim["file"], claim["digest"])
        entry = entries.get(key)
        if entry is None:
            findings.append({"kind": "undispositioned", "file": claim["file"],
                             "line": claim["line"], "text": claim["text"],
                             "why": "no ledger entry backs this claim"})
            continue
        seen.add(key)
        if entry["disposition"] == "withdrawn":
            findings.append({"kind": "withdrawn-present", "file": claim["file"],
                             "line": claim["line"], "text": claim["text"],
                             "why": "a withdrawn claim is on the public surface again"})

    for key, entry in entries.items():
        if entry["disposition"] == "withdrawn" or key in seen:
            continue
        findings.append({"kind": "stale-entry", "file": entry["file"], "line": 0,
                         "text": entry["text"],
                         "why": "the ledger backs a claim the surface no longer makes"})

    # A withdrawn wording must not reappear anywhere in a public file, including
    # inside an example block the sentence reader skips.
    blob = surface_text(root, files, overlay)
    for entry in ledger["claims"]:
        if entry["disposition"] != "withdrawn":
            continue
        needle = flatten(entry["text"])
        if any(needle in text for text in blob):
            findings.append({"kind": "withdrawn-present", "file": entry["file"],
                             "line": 0, "text": entry["text"],
                             "why": "a withdrawn wording is back on the public surface"})

    findings.extend(check_backing(root, ledger, tests))
    return claims, findings


def check_backing(root, ledger, tests):
    """Each disposition's own rule: a gate names a real test, an artifact names a real
    file, a measurement carries its provenance, and a judgement carries no number."""
    findings = []
    for entry in ledger["claims"]:
        disposition, backing = entry["disposition"], entry.get("backing", "")
        where = {"file": entry["file"], "line": 0, "text": entry["text"]}
        if disposition not in DISPOSITIONS:
            findings.append({"kind": "bad-disposition", **where,
                             "why": f"'{disposition}' is not a disposition"})
            continue
        if disposition == "gate":
            # A gate that needs a tool this build does not have is registered only
            # where the tool is present. Those names are listed in the ledger with the
            # condition, so a renamed or deleted test still fails here while a
            # legitimately absent one does not.
            conditional = ledger.get("conditional_tests", {})
            missing = [t for t in backing.split()
                       if t not in tests and t not in conditional]
            if missing:
                findings.append({"kind": "backing-missing", **where,
                                 "why": "no such test: " + ", ".join(missing)})
        elif disposition == "artifact":
            absent = [p for p in backing.split()
                      if not os.path.exists(os.path.join(root, p))]
            if absent:
                findings.append({"kind": "backing-missing", **where,
                                 "why": "no such artifact: " + ", ".join(absent)})
        elif disposition == "measurement":
            findings.extend(check_provenance(entry, where))
        elif disposition == "rationale" and surface.MEASURED_QUANTITY.search(entry["text"]):
            findings.append({"kind": "quantified-judgement", **where,
                             "why": "a claim carrying a measured quantity cannot be "
                                    "recorded as a design judgement"})
    return findings


def check_provenance(entry, where):
    """A measured claim states what produced it, on what, and when."""
    provenance = entry.get("provenance") or {}
    missing = [f for f in PROVENANCE_FIELDS if not provenance.get(f)]
    if missing:
        return [{"kind": "provenance-missing", **where,
                 "why": "a measured claim without " + ", ".join(missing)}]
    if not DATE.match(provenance["obtained"]):
        return [{"kind": "provenance-missing", **where,
                 "why": "obtained is not a date: " + provenance["obtained"]}]
    return []


def reclassified(ledger):
    """Claims that carry a measured quantity and are backed by something other than a
    measurement. Each states why it needs no measurement date of its own, so the
    provenance rule cannot be escaped by relabelling."""
    out = []
    for entry in ledger["claims"]:
        if entry["disposition"] in ("measurement", "withdrawn"):
            continue
        if not surface.MEASURED_QUANTITY.search(entry["text"]):
            continue
        out.append({"file": entry["file"], "text": entry["text"],
                    "disposition": entry["disposition"],
                    "backing": entry.get("backing", ""),
                    "reason": entry.get("reason", "")})
    return out


def build_overlay(kind, root, ledger, tmp):
    """Produce the known-bad input for one injected defect: either a public file with
    a claim that should not pass, or a ledger that dodges a rule."""
    overlay, ledger_out = {}, ledger
    if kind == "unbacked-claim":
        target = "README.md"
        text = open(os.path.join(root, target), encoding="utf-8").read()
        path = os.path.join(tmp, "README.md")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text + "\n" + FABRICATED + "\n")
        overlay[target] = path
    elif kind == "withdrawn-form":
        entry = next((e for e in ledger["claims"] if e["disposition"] == "withdrawn"),
                     None)
        if entry is None:
            raise SystemExit("no withdrawn claim to reintroduce")
        target = entry["file"] if entry["file"].endswith(".md") else "README.md"
        text = open(os.path.join(root, target), encoding="utf-8").read()
        path = os.path.join(tmp, os.path.basename(target))
        with open(path, "w", encoding="utf-8") as f:
            f.write(text + "\n" + entry["text"] + "\n")
        overlay[target] = path
    elif kind in ("stripped-provenance", "reclassified"):
        ledger_out = json.loads(json.dumps(ledger))
        # The relabelling case needs a claim that actually carries a measured quantity,
        # because that is the class the rule is about: a number with a unit, relabelled
        # as a judgement to escape carrying the date it came from.
        wants_quantity = kind == "reclassified"
        entry = next(
            (e for e in ledger_out["claims"]
             if e["disposition"] == "measurement"
             and (not wants_quantity or surface.MEASURED_QUANTITY.search(e["text"]))),
            None)
        if entry is None:
            raise SystemExit("no measured claim to injure with " + kind)
        if kind == "stripped-provenance":
            entry.pop("provenance", None)
        else:
            entry["disposition"] = "rationale"
            entry["backing"] = "a judgement about the project's direction"
            entry.pop("provenance", None)
    else:
        raise SystemExit(f"unknown injection {kind}")
    return overlay, ledger_out


def report(claims, findings, ledger, mode, inject):
    counts = {}
    for entry in ledger["claims"]:
        counts[entry["disposition"]] = counts.get(entry["disposition"], 0) + 1
    expected = sum(1 for e in ledger["claims"] if e["disposition"] != "withdrawn")
    return {
        "tool": "jmpxx-claim-audit",
        "schema": 1,
        "mode": mode,
        "injected": inject or "",
        "cases": {"asked": len(claims), "known": expected},
        "surface": {"files": len({c["file"] for c in claims}),
                    "claims": len(claims)},
        "dispositions": counts,
        "reclassified": reclassified(ledger),
        "findings": findings,
        "ok": not findings,
    }


def render(rep):
    lines = [f"claim audit ({rep['mode']}): {rep['surface']['claims']} claims over "
             f"{rep['surface']['files']} public files"]
    lines.append("    cases.asked  " + str(rep["cases"]["asked"]))
    lines.append("    cases.known  " + str(rep["cases"]["known"]))
    for name in DISPOSITIONS:
        if name in rep["dispositions"]:
            lines.append(f"    {name:12s} {rep['dispositions'][name]}")
    if rep["reclassified"]:
        lines.append(f"    quantified claims backed without a measurement date: "
                     f"{len(rep['reclassified'])}, each with its reason")
    for f in rep["findings"][:40]:
        lines.append(f"  FAIL [{f['kind']}] {f['file']}:{f['line']}: {f['why']}")
        lines.append(f"       {f['text'][:120]}")
    if len(rep["findings"]) > 40:
        lines.append(f"  ... {len(rep['findings']) - 40} more")
    lines.append("  VERDICT: " + ("PASS" if rep["ok"] else "FAIL"))
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="jmpxx public claim audit")
    ap.add_argument("--source-root", required=True)
    ap.add_argument("--ledger", required=True)
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--mode", choices=["audit", "provenance"], default="audit")
    ap.add_argument("--inject", choices=sorted(INJECTIONS))
    ap.add_argument("--report")
    args = ap.parse_args()

    root = os.path.abspath(args.source_root)
    ledger = load_ledger(args.ledger)
    tests = registered_tests(args.build_dir)

    overlay = None
    with tempfile.TemporaryDirectory() as tmp:
        if args.inject:
            overlay, ledger = build_overlay(args.inject, root, ledger, tmp)
        claims, findings = audit(root, ledger, tests, overlay)

    if args.mode == "provenance":
        # The provenance pass answers one question: does every measured claim carry
        # what produced it, and does every quantified claim that is backed some other
        # way say why it needs no measurement of its own.
        findings = [f for f in findings
                    if f["kind"] in ("provenance-missing", "quantified-judgement")]
        findings += [{"kind": "reason-missing", "file": r["file"], "line": 0,
                      "text": r["text"],
                      "why": "a quantified claim backed by a "
                             f"{r['disposition']} states no reason for carrying no "
                             "measurement date"}
                     for r in reclassified(ledger) if not r["reason"]]

    rep = report(claims, findings, ledger, args.mode, args.inject)
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            json.dump(rep, f, indent=1)
    print(render(rep))
    return 0 if rep["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
