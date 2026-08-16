#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Audit the unwind metadata a binary emits for the frames an escape passes through.

The arm runs a destructor during an escape only where the compiler emitted a cleanup
landing pad for that frame and recorded it in the frame's exception table. Destructor
counts prove the cleanups ran on the machine that ran them; this reads the tables the
runtime consults, so a frame that lost its cleanups is visible in the binary itself,
per architecture and per optimization level, without executing anything.

For a DWARF target it parses .eh_frame to find each function's frame description entry
and its language-specific data area, then decodes the call-site table in
.gcc_except_table and reports how many call sites carry a landing pad. For an ARM
exception-handling-ABI target it reads .ARM.exidx and reports which functions have an
unwind entry rather than the cannot-unwind marker, which is the equivalent precondition
there.

Usage:
  unwind_metadata_audit.py BINARY [--require SYMBOL_SUBSTRING ...] [--report FILE]
                                  [--format human|json]

Each --require names a substring of a mangled or demangled symbol. At least one matching
function must carry cleanup landing pads, and a required name that matches nothing is
itself a failure, so a renamed frame cannot silently drop out of the audit. Not every
match needs pads: a helper that owns no automatic object with a destructor has nothing to
clean, and a destructor is itself the cleanup rather than a frame that needs one. The
report names which matches carry pads so the distinction stays visible.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import struct
import subprocess
import sys

DW_EH_PE_omit = 0xFF
DW_EH_PE_uleb128 = 0x01
DW_EH_PE_udata2 = 0x02
DW_EH_PE_udata4 = 0x03
DW_EH_PE_udata8 = 0x04
DW_EH_PE_sleb128 = 0x09
DW_EH_PE_sdata2 = 0x0A
DW_EH_PE_sdata4 = 0x0B
DW_EH_PE_sdata8 = 0x0C


class Reader:
    """A cursor over a section that also knows each byte's virtual address, which the
    pc-relative encodings in the exception tables are measured from."""

    def __init__(self, data: bytes, base_addr: int, pos: int = 0):
        self.data = data
        self.base = base_addr
        self.pos = pos

    @property
    def addr(self) -> int:
        return self.base + self.pos

    def eof(self) -> bool:
        return self.pos >= len(self.data)

    def u8(self) -> int:
        v = self.data[self.pos]
        self.pos += 1
        return v

    def fixed(self, size: int, signed: bool) -> int:
        fmt = {1: "b", 2: "h", 4: "i", 8: "q"}[size]
        if not signed:
            fmt = fmt.upper()
        (v,) = struct.unpack_from("<" + fmt, self.data, self.pos)
        self.pos += size
        return v

    def uleb(self) -> int:
        result = 0
        shift = 0
        while True:
            byte = self.u8()
            result |= (byte & 0x7F) << shift
            if not byte & 0x80:
                return result
            shift += 7

    def sleb(self) -> int:
        result = 0
        shift = 0
        while True:
            byte = self.u8()
            result |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                if byte & 0x40:
                    result -= 1 << shift
                return result

    def cstring(self) -> str:
        end = self.data.index(b"\0", self.pos)
        s = self.data[self.pos:end].decode("ascii", "replace")
        self.pos = end + 1
        return s

    def encoded(self, encoding: int, ptr_size: int, func_start: int = 0) -> int:
        """Decode one pointer in a DWARF exception-header encoding."""
        if encoding == DW_EH_PE_omit:
            return 0
        here = self.addr
        fmt = encoding & 0x0F
        if fmt == 0x00:  # absptr
            value = self.fixed(ptr_size, False)
        elif fmt == DW_EH_PE_uleb128:
            value = self.uleb()
        elif fmt == DW_EH_PE_sleb128:
            value = self.sleb()
        elif fmt in (DW_EH_PE_udata2, DW_EH_PE_udata4, DW_EH_PE_udata8):
            value = self.fixed(1 << (fmt - 1), False)
        elif fmt in (DW_EH_PE_sdata2, DW_EH_PE_sdata4, DW_EH_PE_sdata8):
            value = self.fixed(1 << (fmt - DW_EH_PE_sdata2 + 1), True)
        else:
            raise ValueError(f"unsupported pointer encoding 0x{encoding:02x}")
        app = encoding & 0x70
        if app == 0x10:  # pcrel
            value += here
        elif app == 0x40:  # funcrel
            value += func_start
        elif app not in (0x00, 0x30):  # absolute and datarel need no adjustment here
            raise ValueError(f"unsupported pointer application 0x{encoding:02x}")
        return value & ((1 << (8 * ptr_size)) - 1)


@dataclasses.dataclass
class Section:
    name: str
    addr: int
    offset: int
    size: int
    type: int
    data: bytes


@dataclasses.dataclass
class Symbol:
    name: str
    value: int
    size: int


class Elf:
    def __init__(self, path: str):
        with open(path, "rb") as f:
            self.raw = f.read()
        if self.raw[:4] != b"\x7fELF":
            raise ValueError(f"{path} is not an ELF file")
        self.is64 = self.raw[4] == 2
        self.ptr_size = 8 if self.is64 else 4
        self.machine = struct.unpack_from("<H", self.raw, 18)[0]
        if self.is64:
            e_shoff, = struct.unpack_from("<Q", self.raw, 0x28)
            e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", self.raw, 0x3A)
        else:
            e_shoff, = struct.unpack_from("<I", self.raw, 0x20)
            e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", self.raw, 0x2E)
        raw_sections = []
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            if self.is64:
                name, stype, _flags, addr, offset, size = struct.unpack_from(
                    "<IIQQQQ", self.raw, off)
            else:
                name, stype, _flags, addr, offset, size = struct.unpack_from(
                    "<IIIIII", self.raw, off)
            raw_sections.append((name, stype, addr, offset, size))
        shstr_off = raw_sections[e_shstrndx][3]
        shstr_size = raw_sections[e_shstrndx][4]
        shstr = self.raw[shstr_off:shstr_off + shstr_size]

        def sec_name(idx: int) -> str:
            end = shstr.index(b"\0", idx)
            return shstr[idx:end].decode("ascii", "replace")

        self.sections = {}
        for name, stype, addr, offset, size in raw_sections:
            nm = sec_name(name)
            data = b"" if stype == 8 else self.raw[offset:offset + size]  # 8 = NOBITS
            self.sections[nm] = Section(nm, addr, offset, size, stype, data)
        self.symbols = self._read_symbols(e_shoff, e_shentsize, e_shnum)

    def _read_symbols(self, e_shoff, e_shentsize, e_shnum):
        symbols = []
        for tab, strtab in ((".symtab", ".strtab"), (".dynsym", ".dynstr")):
            if tab not in self.sections or strtab not in self.sections:
                continue
            data = self.sections[tab].data
            names = self.sections[strtab].data
            entry = 24 if self.is64 else 16
            for off in range(0, len(data) - entry + 1, entry):
                if self.is64:
                    st_name, st_info, _o, _shndx, st_value, st_size = struct.unpack_from(
                        "<IBBHQQ", data, off)
                else:
                    st_name, st_value, st_size, st_info, _o, _shndx = struct.unpack_from(
                        "<IIIBBH", data, off)
                if (st_info & 0xF) != 2 or st_value == 0:  # 2 = STT_FUNC
                    continue
                end = names.index(b"\0", st_name)
                symbols.append(Symbol(names[st_name:end].decode("ascii", "replace"),
                                      st_value, st_size))
            if symbols:
                break
        return symbols


@dataclasses.dataclass
class FrameInfo:
    pc_begin: int
    pc_range: int
    lsda: int


def parse_eh_frame(elf: Elf) -> list[FrameInfo]:
    """Return one entry per frame description entry, with its exception-table address."""
    sec = elf.sections.get(".eh_frame")
    if sec is None or not sec.data:
        return []
    frames = []
    cies: dict[int, tuple[int, int]] = {}  # offset -> (fde_encoding, lsda_encoding)
    r = Reader(sec.data, sec.addr)
    while not r.eof():
        record_start = r.pos
        length = r.fixed(4, False)
        if length == 0:
            break
        if length == 0xFFFFFFFF:
            raise ValueError("64-bit .eh_frame records are not supported")
        end = r.pos + length
        cie_field_pos = r.pos
        cie_id = r.fixed(4, False)
        if cie_id == 0:
            version = r.u8()
            aug = r.cstring()
            r.uleb()  # code alignment factor
            r.sleb()  # data alignment factor
            if version >= 3:
                r.uleb()
            else:
                r.u8()
            fde_enc, lsda_enc = 0x00, DW_EH_PE_omit
            if aug.startswith("z"):
                aug_len = r.uleb()
                aug_end = r.pos + aug_len
                for ch in aug[1:]:
                    if ch == "R":
                        fde_enc = r.u8()
                    elif ch == "L":
                        lsda_enc = r.u8()
                    elif ch == "P":
                        enc = r.u8()
                        r.encoded(enc, elf.ptr_size)
                    elif ch == "S":
                        pass
                    else:
                        break
                r.pos = aug_end
            cies[record_start] = (fde_enc, lsda_enc)
        else:
            cie_offset = cie_field_pos - cie_id
            fde_enc, lsda_enc = cies.get(cie_offset, (0x00, DW_EH_PE_omit))
            pc_begin = r.encoded(fde_enc, elf.ptr_size)
            pc_range = r.encoded(fde_enc & 0x0F, elf.ptr_size)
            lsda = 0
            if lsda_enc != DW_EH_PE_omit:
                aug_len = r.uleb()
                aug_end = r.pos + aug_len
                if aug_len:
                    lsda = r.encoded(lsda_enc, elf.ptr_size, pc_begin)
                r.pos = aug_end
            frames.append(FrameInfo(pc_begin, pc_range, lsda))
        r.pos = end
    return frames


def parse_lsda(elf: Elf, lsda_addr: int, func_start: int) -> dict:
    """Decode one language-specific data area's call-site table."""
    sec = elf.sections.get(".gcc_except_table")
    if sec is None or not (sec.addr <= lsda_addr < sec.addr + sec.size):
        return {"call_sites": 0, "with_landing_pad": 0, "cleanup_sites": 0,
                "decoded": False}
    r = Reader(sec.data, sec.addr, lsda_addr - sec.addr)
    lp_enc = r.u8()
    lp_start = func_start
    if lp_enc != DW_EH_PE_omit:
        lp_start = r.encoded(lp_enc, elf.ptr_size, func_start)
    ttype_enc = r.u8()
    if ttype_enc != DW_EH_PE_omit:
        r.uleb()  # offset to the type table, which this audit does not need
    cs_enc = r.u8()
    cs_len = r.uleb()
    cs_end = r.pos + cs_len
    sites = with_pad = cleanups = 0
    while r.pos < cs_end:
        r.encoded(cs_enc, elf.ptr_size)  # region start, relative to the landing-pad base
        r.encoded(cs_enc, elf.ptr_size)  # region length
        pad = r.encoded(cs_enc, elf.ptr_size)
        action = r.uleb()
        sites += 1
        if pad:
            with_pad += 1
            if action == 0:
                cleanups += 1
    return {"call_sites": sites, "with_landing_pad": with_pad,
            "cleanup_sites": cleanups, "decoded": True, "lp_start": lp_start}


def parse_arm_exidx(elf: Elf) -> dict[int, bool]:
    """Return {function address: has an unwind entry} from .ARM.exidx."""
    sec = elf.sections.get(".ARM.exidx")
    if sec is None or not sec.data:
        return {}
    out = {}
    for off in range(0, len(sec.data) - 7, 8):
        word0, word1 = struct.unpack_from("<II", sec.data, off)
        here = sec.addr + off
        # prel31: a sign-extended 31-bit offset from the word's own address.
        delta = word0 & 0x7FFFFFFF
        if delta & 0x40000000:
            delta -= 0x80000000
        out[here + delta] = word1 != 1  # 1 is EXIDX_CANTUNWIND
    return out


def demangle(names: list[str]) -> dict[str, str]:
    if not names:
        return {}
    try:
        out = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True,
                             text=True, check=True).stdout.splitlines()
        return dict(zip(names, out))
    except (OSError, subprocess.CalledProcessError):
        return {n: n for n in names}


def audit(path: str, required: list[str]) -> dict:
    elf = Elf(path)
    frames = parse_eh_frame(elf)
    exidx = parse_arm_exidx(elf)
    fmt = "arm_exidx" if exidx and not frames else "dwarf_eh_frame"

    by_addr = {}
    for f in frames:
        by_addr[f.pc_begin] = f
    pretty = demangle([s.name for s in elf.symbols])

    functions = []
    for sym in elf.symbols:
        # A cold clone holds the landing-pad body of its parent rather than a frame of
        # its own, so it is not a frame the audit expects cleanups for.
        if ".cold" in sym.name:
            continue
        record = {"symbol": sym.name, "demangled": pretty.get(sym.name, sym.name),
                  "address": sym.value}
        if fmt == "arm_exidx":
            record["unwindable"] = exidx.get(sym.value, False)
            record["has_cleanups"] = bool(record["unwindable"])
        else:
            frame = by_addr.get(sym.value)
            record["has_frame"] = frame is not None
            record["lsda"] = bool(frame and frame.lsda)
            if frame and frame.lsda:
                record.update(parse_lsda(elf, frame.lsda, frame.pc_begin))
            record["has_cleanups"] = bool(record.get("cleanup_sites", 0))
        functions.append(record)

    checks = []
    for want in required:
        matches = [f for f in functions
                   if want in f["symbol"] or want in f["demangled"]]
        with_cleanups = [f for f in matches if f["has_cleanups"]]
        checks.append({
            "required": want,
            "matched": len(matches),
            "with_cleanups": len(with_cleanups),
            "ok": bool(with_cleanups),
            "carrying": sorted(f["demangled"] for f in with_cleanups),
            "without": sorted(f["demangled"] for f in matches if not f["has_cleanups"]),
        })

    return {
        "tool": "jmpxx-unwind-metadata-audit",
        "schema": 1,
        "binary": path,
        "format": fmt,
        "frames": len(frames),
        "functions_with_cleanups": sum(1 for f in functions if f["has_cleanups"]),
        "cleanup_functions": sorted(f["demangled"] for f in functions
                                    if f["has_cleanups"]),
        "checks": checks,
        "verdict": "pass" if all(c["ok"] for c in checks) else "fail",
    }


def render_human(rep: dict) -> str:
    lines = [f"unwind metadata audit: {rep['binary']} ({rep['format']}, "
             f"{rep['frames']} frame entries, "
             f"{rep['functions_with_cleanups']} functions with cleanups)"]
    if rep.get("list_cleanups"):
        for name in rep["cleanup_functions"]:
            lines.append(f"    cleanups: {name}")
    for c in rep["checks"]:
        mark = "OK  " if c["ok"] else "FAIL"
        lines.append(f"  [{mark}] {c['required']}: {c['with_cleanups']}/{c['matched']} "
                     f"matching functions carry cleanup landing pads")
        if not c["matched"]:
            lines.append("           no function matched this name")
        elif not c["with_cleanups"]:
            for m in c["without"]:
                lines.append(f"           no cleanup landing pad: {m}")
    lines.append(f"  VERDICT: {rep['verdict'].upper()}")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description="audit emitted unwind metadata")
    ap.add_argument("binary")
    ap.add_argument("--require", action="append", default=[],
                    help="symbol substring that must carry cleanup landing pads")
    ap.add_argument("--report")
    ap.add_argument("--format", choices=["human", "json"], default="human")
    ap.add_argument("--list", action="store_true",
                    help="also print every function that carries cleanup landing pads")
    args = ap.parse_args()

    rep = audit(args.binary, args.require)
    rep["list_cleanups"] = args.list
    text = json.dumps(rep, indent=2) if args.format == "json" else render_human(rep)
    if args.report:
        with open(args.report, "w") as f:
            json.dump(rep, f, indent=2)
    print(text)
    return 0 if rep["verdict"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
