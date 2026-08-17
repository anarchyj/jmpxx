#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Find a fact that has acquired a second home on the public surface.

Duplication across surfaces is the mechanism behind a stale claim: one copy gets
corrected, the others do not, and a reader cannot tell which is canonical. This gate
compares every claim-bearing sentence against every claim-bearing sentence in a
different file and reports the pairs that say the same thing.

The measure is the overlap of content words, which catches a restatement that has
been reworded as well as a copy. Two sentences that share most of their content words
are two homes for one fact even when neither was pasted from the other.

An allowlisted pair carries the reason the restatement is deliberate. The allowlist is
the escape hatch and it is bounded on purpose: a list long enough to admit the
duplication this gate was built to find is a list that has stopped being a gate.

Usage:
  canonical_home.py --source-root R --allowlist F [--threshold F] [--report J]
                    [--inject]
"""
import argparse
import itertools
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import claim_surface as surface  # noqa: E402

# Words that carry no subject. Two sentences that share only these share nothing.
STOPWORDS = set(
    "a an the of to in on for and or is are be as that this it its by with from at "
    "not no but so if then than which who whom whose there here when where what how "
    "why can may must will would should could each every any all one two both into "
    "over under while does do done has have had was were been being their them they "
    "you your we our us he she his her".split())
WORD = re.compile(r"[a-z_][a-z0-9_:+.]*")

# A sentence with few content words says too little for an overlap to mean anything.
MIN_CONTENT_WORDS = 8


def content(text):
    return {w for w in WORD.findall(text.lower())
            if w not in STOPWORDS and len(w) > 2}


def overlap(a, b):
    return len(a & b) / len(a | b) if a | b else 0.0


def pairs(claims, threshold, exempt):
    """Every cross-file pair of claims whose content words overlap at or above the
    threshold, most alike first. An exempt file is one whose restatement is a record
    rather than a second home; the allowlist names each and why."""
    prepared = [(c, content(c["text"])) for c in claims
                if c["file"] not in exempt]
    prepared = [(c, w) for c, w in prepared if len(w) >= MIN_CONTENT_WORDS]
    found = []
    for (a, wa), (b, wb) in itertools.combinations(prepared, 2):
        if a["file"] == b["file"]:
            continue
        score = overlap(wa, wb)
        if score >= threshold:
            found.append({"similarity": round(score, 3),
                          "a": {"file": a["file"], "line": a["line"],
                                "digest": a["digest"], "text": a["text"]},
                          "b": {"file": b["file"], "line": b["line"],
                                "digest": b["digest"], "text": b["text"]}})
    found.sort(key=lambda p: -p["similarity"])
    return found


def allowed(pair, allowlist):
    key = {pair["a"]["digest"], pair["b"]["digest"]}
    for entry in allowlist["pairs"]:
        if key == set(entry["digests"]):
            return entry
    return None


def main():
    ap = argparse.ArgumentParser(description="jmpxx canonical-home check")
    ap.add_argument("--source-root", required=True)
    ap.add_argument("--allowlist", required=True)
    ap.add_argument("--threshold", type=float)
    ap.add_argument("--report")
    ap.add_argument("--inject", action="store_true",
                    help="reintroduce a duplicate pair the tree once carried, which "
                         "the gate must report")
    args = ap.parse_args()

    with open(args.allowlist, encoding="utf-8") as f:
        allowlist = json.load(f)
    threshold = args.threshold or allowlist["threshold"]

    claims = surface.claims(os.path.abspath(args.source_root))
    if args.inject:
        # The known-bad input: a sentence that already has a home, restated on a
        # second surface. The gate exists to see exactly this.
        subject = allowlist["injected_duplicate"]
        claims.append({"file": "README.md", "line": 0, "kind": "prose",
                       "text": subject, "digest": surface.digest(subject)})

    found = pairs(claims, threshold, allowlist.get("exempt_files", {}))
    unexplained, explained = [], []
    for pair in found:
        entry = allowed(pair, allowlist)
        if entry:
            explained.append({**pair, "reason": entry["reason"]})
        else:
            unexplained.append(pair)

    ceiling = allowlist.get("allowlist_ceiling", 0)
    findings = list(unexplained)
    if len(allowlist["pairs"]) > ceiling:
        findings.append({"similarity": 0,
                         "a": {"file": args.allowlist, "line": 0, "text": ""},
                         "b": {"file": args.allowlist, "line": 0, "text": ""},
                         "why": f"{len(allowlist['pairs'])} allowlisted pairs, above "
                                f"the ceiling of {ceiling}"})

    rep = {"tool": "jmpxx-canonical-home", "schema": 1,
           "threshold": threshold,
           "cases": {"asked": len(claims), "known": len(claims)},
           "duplicate_pairs": len(found),
           "allowlisted": explained,
           "findings": findings,
           "ok": not findings}
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            json.dump(rep, f, indent=1)

    print(f"canonical home: {len(claims)} claims compared across surfaces at "
          f"similarity {threshold}")
    print(f"    cases.asked  {len(claims)}")
    print(f"    cases.known  {len(claims)}")
    print(f"    duplicate pairs: {len(found)} ({len(explained)} allowlisted)")
    for pair in findings[:30]:
        if "why" in pair:
            print(f"  FAIL {pair['why']}")
            continue
        print(f"  FAIL a fact with two homes ({pair['similarity']}):")
        print(f"       {pair['a']['file']}:{pair['a']['line']}  {pair['a']['text'][:110]}")
        print(f"       {pair['b']['file']}:{pair['b']['line']}  {pair['b']['text'][:110]}")
    print("  VERDICT: " + ("PASS" if rep["ok"] else "FAIL"))
    return 0 if rep["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
