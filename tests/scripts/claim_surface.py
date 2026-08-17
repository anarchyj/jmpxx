#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The public claim surface: which files jmpxx makes claims in, and how a claim is
found inside one.

A claim is a sentence that asserts a checkable property of jmpxx: a measured
quantity, an absolute guarantee, a behavioural contract, or a statement about what a
gate proves. This module locates those sentences; `claim_audit.py` decides whether
each one is backed and `canonical_home.py` decides whether one has a second home.
Both read the surface through here so the two gates cannot disagree about what the
surface is.

Extraction errs toward finding too much. A sentence that is picked up and is not
really a claim costs one ledger line; a sentence that is missed is the failure the
audit exists to prevent, so the trade is deliberate.
"""
import hashlib
import os
import re

# Public prose: what a consumer or contributor reads. The three authoring standards
# under docs/ govern how this project writes and are not part of the shipped surface.
PROSE_ROOTS = [
    ("README.md", None),
    ("CHANGELOG.md", None),
    ("CONTRIBUTING.md", None),
    ("SECURITY.md", None),
    ("docs", ".md"),
    ("examples/README.md", None),
    ("reference_app", ".md"),
]
PROSE_EXCLUDE = {
    "docs/writing-style.md",
    "docs/writing-brief.v2.md",
    "docs/code-principles.md",
}

# Public code: the headers a consumer compiles and the example programs read as
# documentation. single_include/ is generated from include/ and is diffed against it
# by the amalgamation gate, so auditing it would double every header claim.
CODE_ROOTS = [("include", ".hpp"), ("examples", ".cpp")]

# A sentence carries a claim when it quantifies, when it asserts something absolute,
# or when it names the artifact that proves it. Each pattern below is one of those
# three, and a sentence matching any of them is extracted.
QUANTITY = re.compile(
    r"\b\d+(?:[.,]\d+)?\s*(?:byte|bytes|bit|bits|instruction|instructions|frame|frames|"
    r"hop|hops|percent|%|times|x|ms|us|ns|line|lines|entr|thread|threads|case|cases|"
    r"level|levels|word|words)"
    r"|\b(?:one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|sixteen|"
    r"twenty|twenty-four|thirty|ninety|hundred|thousand)\s+"
    r"(?:byte|bytes|bit|bits|instruction|instructions|frame|frames|hop|hops|percent|"
    r"times|case|cases|thread|threads|word|words|per\b)"
    r"|\bzero[- ](?:byte|bytes|cost|overhead|allocation|allocations|dependencies|"
    r"dependency)\b"
    r"|\b\d+\s*(?:to|and)\s*\d+\b"
    r"|\bC\+\+\d\d\b"
    r"|\b\d+(?:\.\d+)+\b",
    re.I,
)
# A measured quantity, as distinct from the broad QUANTITY heuristic above: a number
# with a unit something was measured in. A version, a date, and a standard's name are
# numbers and are not measurements, so a sentence about them is not held to the
# provenance rule.
MEASURED_QUANTITY = re.compile(
    r"\b\d+(?:[.,]\d+)?\s*(?:byte|bytes|instruction|instructions|frame|frames|hop|hops|"
    r"percent|%|times|ns|us|ms|nanosecond|microsecond|millisecond|allocation|allocations|"
    r"thread|threads|template|templates|mutant|mutants|case|cases)\b"
    r"|\b(?:one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|sixteen|"
    r"twenty|twenty-four|thirty|ninety|hundred|thousand)[- ]"
    r"(?:byte|bytes|instruction|instructions|frame|frames|hop|hops|percent|times|"
    r"thread|threads|fold)\b"
    r"|\bzero[- ](?:byte|bytes|allocation|allocations|overhead)\b",
    re.I,
)
ABSOLUTE = re.compile(
    r"\b(?:never|always|every|exactly|identical|identically|cannot|no(?:thing|ne)?|"
    r"zero|lossless|losslessly|unchanged|guarantee|guarantees|guaranteed|ensures|"
    r"refuses|refuse|terminates|fails|fail|free of|without|only|must|preserved|"
    r"preserves|allocates nothing|compiles to nothing|does not|is not|are not)\b",
    re.I,
)
BACKING = re.compile(
    r"\b(?:gate|gates|gated|tier|tiers|golden|goldens|probe|probes|jmpxx-verify|"
    r"jmpxx-bench|jmpxx-lint|ctest|sanitizer|sanitizers|fuzz|fuzzing|measured|"
    r"measures|verified|verifies|proven|proves|enforced|enforces|checked|checks|"
    r"reports|test|tests)\b",
    re.I,
)

# Lines that carry no claim of their own: a heading, a table rule, an image, a
# comment, and a link-reference definition, which is an address rather than a
# sentence.
SKIP_LINE = re.compile(
    r"^\s*(?:#{1,6}\s|\||!\[|<!--|-{3,}\s*$|\|?[\s:|-]+\|\s*$|\[[^\]]+\]:\s)"
)
LINK_DEFINITION = re.compile(r"^\s*\[[^\]]+\]:\s+\S+")
TABLE_SEPARATOR = re.compile(r"^\s*\|?[\s:|-]+\|[\s:|-]*$")

# Markup that is not part of what the sentence claims. Underscores are left alone:
# this project's prose emphasises with asterisks and backticks, and stripping them
# would turn and_then and std::error_code into different words than the ones the
# surface actually says.
LINK = re.compile(r"\[([^\]]+)\]\([^)]*\)")
HTML_COMMENT = re.compile(r"<!--.*?-->", re.S)
INLINE_MARKUP = re.compile(r"[*`]")

SENTENCE_END = re.compile(r"(?<=[.!?])\s+")
# A period between digits (0.1.4), after an abbreviation, or inside an identifier is
# not a sentence boundary.
NOT_A_BOUNDARY = re.compile(r"(?:\b(?:e\.g|i\.e|vs|no|cf|approx)\.|\d\.\d)$", re.I)

MIN_TOKENS = 5


def _clean(text):
    """Prose as the reader sees it: link text without its target, emphasis and code
    spans without their markers."""
    text = LINK.sub(r"\1", text)
    return " ".join(INLINE_MARKUP.sub("", text).split())


def _verbatim(text):
    """Code comments carry no markup, so only the line wrapping is normalized."""
    return " ".join(text.split())


def _split_sentences(text):
    """Split on sentence boundaries, keeping version numbers and abbreviations whole."""
    parts, current = [], ""
    for piece in SENTENCE_END.split(text):
        candidate = (current + " " + piece).strip() if current else piece
        if NOT_A_BOUNDARY.search(candidate):
            current = candidate
            continue
        parts.append(candidate)
        current = ""
    if current:
        parts.append(current)
    return [p.strip() for p in parts if p.strip()]


def is_claim(sentence):
    """Whether a sentence asserts something the project must be able to back."""
    if len(sentence.split()) < MIN_TOKENS:
        return False
    # MEASURED_QUANTITY is consulted as well as QUANTITY so that the narrow pattern is
    # a subset of the broad one by construction. They drifted apart: a count of
    # templates read as a measurement to the provenance rule and as nothing at all to
    # the extractor, so the comparison's headline compile cost was never a claim and
    # never needed backing.
    return bool(QUANTITY.search(sentence) or MEASURED_QUANTITY.search(sentence)
                or ABSOLUTE.search(sentence) or BACKING.search(sentence))


def digest(sentence):
    """A stable identity for a claim's wording. Editing the wording changes it, which
    is what forces an edited claim back through the audit."""
    normalized = " ".join(sentence.split()).lower()
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:12]


def _prose_sentences(text):
    """Sentences from markdown: prose paragraphs, list items, and table rows. Fenced
    code is the example, not the claim, so it is skipped."""
    text = HTML_COMMENT.sub("", text)
    out = []
    fenced = False
    paragraph = []
    lines = text.splitlines()
    for lineno, raw in enumerate(lines, 1):
        if raw.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if fenced:
            continue
        line = raw.rstrip()
        if LINK_DEFINITION.match(line):
            if paragraph:
                out.extend(_locate(paragraph, _clean))
                paragraph = []
            continue
        if not line.strip() or SKIP_LINE.match(line):
            # A table row is one claim even though it is not a sentence: the README's
            # guarantee table and the comparison's number tables carry the strongest
            # claims on the surface, one per row. The heading row, which the separator
            # under it identifies, names the columns and claims nothing.
            heading = (lineno < len(lines)
                       and TABLE_SEPARATOR.match(lines[lineno]) is not None)
            if (line.strip().startswith("|") and not TABLE_SEPARATOR.match(line)
                    and not heading):
                row = " ".join(c.strip() for c in line.strip().strip("|").split("|"))
                out.append((lineno, _clean(row)))
            if paragraph:
                out.extend(_locate(paragraph, _clean))
                paragraph = []
            continue
        paragraph.append((lineno, line))
    if paragraph:
        out.extend(_locate(paragraph, _clean))
    return [(n, s) for n, s in out if s]


def _locate(numbered_lines, clean):
    """Split a run of numbered lines into sentences, each reported at the line it
    starts on. A block is joined before splitting because a sentence commonly spans
    the wrap, and the line is then recovered by matching the sentence's opening words
    against the lines in order."""
    joined = " ".join(line for _, line in numbered_lines)
    sentences = _split_sentences(clean(joined))
    cleaned = [(n, clean(line).lower()) for n, line in numbered_lines]
    located, cursor = [], 0
    for sentence in sentences:
        head = " ".join(sentence.split()[:4]).lower()[:20]
        line_no = numbered_lines[cursor][0] if cursor < len(cleaned) else numbered_lines[0][0]
        for offset in range(cursor, len(cleaned)):
            if head and head in cleaned[offset][1]:
                line_no = cleaned[offset][0]
                cursor = offset
                break
        located.append((line_no, sentence))
    return located


COMMENT_LINE = re.compile(r"^\s*//\s?(.*)$")
# A license tag is metadata rather than a sentence about the library.
LICENSE_TAG = re.compile(r"^SPDX-\S+:")


def _code_sentences(text):
    """Sentences from C++ comments. Consecutive // lines form one comment block, which
    is then split into sentences the same way prose is."""
    out = []
    block = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        m = COMMENT_LINE.match(raw)
        if m:
            body = m.group(1).strip()
            if body and not LICENSE_TAG.match(body):
                block.append((lineno, body))
            continue
        if block:
            out.extend(_locate(block, _verbatim))
            block = []
    if block:
        out.extend(_locate(block, _verbatim))
    return out


def _walk(root, path, suffix):
    full = os.path.join(root, path)
    if os.path.isfile(full):
        yield path
        return
    for base, _, names in os.walk(full):
        for name in sorted(names):
            if suffix and not name.endswith(suffix):
                continue
            rel = os.path.relpath(os.path.join(base, name), root)
            yield rel.replace(os.sep, "/")


def surface_files(root):
    """Every file on the public claim surface, prose first, as (path, kind)."""
    files = []
    for path, suffix in PROSE_ROOTS:
        for rel in _walk(root, path, suffix):
            if rel not in PROSE_EXCLUDE:
                files.append((rel, "prose"))
    for path, suffix in CODE_ROOTS:
        for rel in _walk(root, path, suffix):
            files.append((rel, "code"))
    return files


def claims(root, files=None):
    """Every claim on the public surface as dicts with file, line, text, digest."""
    found = []
    for rel, kind in (files if files is not None else surface_files(root)):
        with open(os.path.join(root, rel), encoding="utf-8") as f:
            text = f.read()
        sentences = _prose_sentences(text) if kind == "prose" else _code_sentences(text)
        for line, sentence in sentences:
            if is_claim(sentence):
                found.append({"file": rel, "line": line, "text": sentence,
                              "digest": digest(sentence), "kind": kind})
    return found
