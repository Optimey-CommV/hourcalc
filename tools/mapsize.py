# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later

r"""Attribute the bytes of a linked HourCalc.exe to objects and libraries, from a linker map.

    link ... /MAP:hc.map            (any build; the map lists every public and static symbol)
    python tools\mapsize.py hc.map [HourCalc.exe] [--top N]

Sizes are taken as the distance from a symbol to the next symbol with a higher address in the
same segment, so a symbol that was folded into another one (identical COMDAT folding) costs
nothing and the bytes between two symbols belong to the first. With the exe the real section
table is printed too and the .pdata entries are counted per object.

Output: the section table, then for .text, .rdata (read-only data, unwind information and
import tables), .data and .pdata the bytes per object and per group (the application, the
core library, the static C runtime, vcruntime, the C++ library, import thunks).
"""

import argparse
import collections
import re
import struct
import sys

SYM = re.compile(r"^ ([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{16})\s+(?:f\s+)?(?:i\s+)?(\S.*?)\s*$")
SEC = re.compile(r"^ ([0-9a-f]{4}):([0-9a-f]{8}) ([0-9a-f]{8})H (\S+)\s+(\w+)")


def parse_map(path):
    sections, syms = [], []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = SEC.match(line)
            if m and not syms:
                sections.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16), m.group(4), m.group(5)))
                continue
            m = SYM.match(line)
            if m:
                seg = int(m.group(1), 16)
                if seg == 0:
                    continue
                syms.append((seg, int(m.group(2), 16), m.group(3), m.group(5)))
    return sections, syms


def group_of(obj):
    if obj.startswith("hc_core:"):
        return "core library (hc_core.lib)"
    if obj.startswith("LIBCMT:"):
        return "static CRT startup (LIBCMT)"
    if obj.startswith("libvcruntime:"):
        return "vcruntime (EH, memcpy, ...)"
    if obj.startswith("libcpmt:"):
        return "C++ library (libcpmt)"
    if obj.startswith("libucrt:") or obj.startswith("libucrt"):
        return "static UCRT"
    if obj.startswith("ucrt:"):
        return "UCRT import thunks"
    if obj.startswith("* CIL"):
        return "LTCG generated (CIL)"
    if obj.startswith("delayimp:"):
        return "delay-load helper"
    if ":" in obj:
        lib = obj.split(":", 1)[0]
        if lib.lower().endswith(".lib") or lib.lower() in ("kernel32", "user32", "gdi32", "advapi32", "gdiplus", "dwrite", "dwmapi", "comdlg32", "imm32", "oleaut32", "shell32", "ole32", "ntdll"):
            return "import thunks (" + lib + ")"
        return "library " + lib
    if obj.startswith("<exception"):
        return "exception tables (.pdata)"
    if obj in ("<common>", "<linker-defined>", "<absolute>"):
        return "linker"
    return "application " + obj


def sizes(sections, syms):
    segend = collections.defaultdict(int)
    segstart = {}
    for seg, off, length, name, cls in sections:
        segend[seg] = max(segend[seg], off + length)
    by_seg = collections.defaultdict(list)
    # .pdata has no symbols; when it is merged into .rdata it must not be charged to the last symbol before it.
    for seg, off, length, name, cls in sections:
        if name == ".pdata" and cls == "DATA" and off > 0:
            by_seg[seg].append((off, ".pdata", "<exception tables (.pdata)>"))
    for seg, off, name, obj in syms:
        if off >= segend[seg]:
            continue          # import-library placeholders that the map lists with an address outside the segment
        by_seg[seg].append((off, name, obj))
    out = collections.defaultdict(list)       # seg -> [(off, size, name, obj)]
    for seg, items in by_seg.items():
        items.sort(key=lambda t: t[0])
        # one entry per address: the first listed (folded duplicates cost nothing)
        uniq = []
        last = None
        for off, name, obj in items:
            if off == last:
                continue
            uniq.append((off, name, obj))
            last = off
        for i, (off, name, obj) in enumerate(uniq):
            nxt = uniq[i + 1][0] if i + 1 < len(uniq) else segend[seg]
            out[seg].append((off, nxt - off, name, obj))
    return out


def pe_sections(exe):
    d = open(exe, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    base = pe + 24 + opt
    secs = []
    for i in range(nsec):
        name, vsize, va, rsize, rptr = struct.unpack_from("<8sIIII", d, base + 40 * i)
        secs.append((name.rstrip(b"\0").decode(), vsize, va, rsize, rptr))
    return d, secs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("exe", nargs="?")
    ap.add_argument("--top", type=int, default=25)
    args = ap.parse_args()

    sections, syms = parse_map(args.map)
    sz = sizes(sections, syms)
    segnames = {}
    for seg, off, length, name, cls in sections:
        segnames.setdefault(seg, []).append(name)

    print("## Sections of the image")
    if args.exe:
        d, secs = pe_sections(args.exe)
        print("| section | virtual size | raw size |")
        print("|---|---:|---:|")
        for name, vsize, va, rsize, rptr in secs:
            print("| %s | %d | %d |" % (name, vsize, rsize))
        print("| file | %d | |" % len(d))
        print()

    for seg, title in ((1, ".text"), (2, ".rdata (data, unwind info, imports)"), (3, ".data / .bss")):
        if seg not in sz:
            continue
        groups = collections.Counter()
        objs = collections.Counter()
        for off, size, name, obj in sz[seg]:
            groups[group_of(obj)] += size
            objs[obj] += size
        total = sum(groups.values())
        print("## %s: %d bytes attributed" % (title, total))
        print()
        print("| group | bytes | share |")
        print("|---|---:|---:|")
        for g, n in groups.most_common():
            print("| %s | %d | %.1f%% |" % (g, n, 100.0 * n / total))
        print()
        print("| object | bytes |")
        print("|---|---:|")
        for o, n in objs.most_common(args.top):
            print("| %s | %d |" % (o, n))
        print()
        kinds = collections.Counter()
        if seg == 2:
            for off, size, name, obj in sz[seg]:
                if name.startswith("$unwind$") or name.startswith("$chain$") or name.startswith("$xdatasym"):
                    kinds["unwind info"] += size
                elif name.startswith("$cppxdata$") or name.startswith("$stateUnwindMap$") or name.startswith("$ip2state$") or name.startswith("$tryMap$") or name.startswith("$handlerMap$") or name.startswith("$unwindtable") or name.startswith("$tryblock"):
                    kinds["C++ EH tables"] += size
                elif name.startswith("??_C@"):
                    kinds["string literals"] += size
                elif name.startswith("??_7"):
                    kinds["vtables"] += size
                elif name.startswith("__IMPORT_DESCRIPTOR") or name.startswith("\x7f") or ".dll" in obj and ("idata" in name or "IMPORT" in name):
                    kinds["import tables"] += size
                elif name.startswith(".idata"):
                    kinds["import tables"] += size
                elif name.startswith("__imp_"):
                    kinds["import address table"] += size
                else:
                    kinds["other data"] += size
            print("| kind of .rdata symbol | bytes |")
            print("|---|---:|")
            for k, n in kinds.most_common():
                print("| %s | %d |" % (k, n))
            print()
        if args.top and seg == 1:
            print("| largest functions | bytes | object |")
            print("|---|---:|---|")
            for off, size, name, obj in sorted(sz[seg], key=lambda t: -t[1])[:args.top]:
                print("| %s | %d | %s |" % (name[:110].replace("|", "\\|"), size, obj))
            print()

    if args.exe:
        # .pdata: 12 bytes per function; count the entries per object using the function start addresses. The
        # exception directory is found through the data directories, so this also works when .pdata is merged.
        d, secs = pe_sections(args.exe)
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        exc_rva, exc_size = struct.unpack_from("<II", d, pe + 24 + 112 + 3 * 8)
        text = [s for s in secs if s[0] == ".text"][0]
        owner = [s for s in secs if s[2] <= exc_rva < s[2] + max(s[1], s[3])]
        if exc_size and owner:
            name, vsize, va, rsize, rptr = owner[0]
            base = rptr + (exc_rva - va)
            n = exc_size // 12
            text_syms = sorted((off, obj) for off, size, nm, obj in sz[1])
            starts = [t[0] for t in text_syms]
            import bisect
            counts = collections.Counter()
            for i in range(n):
                begin = struct.unpack_from("<I", d, base + 12 * i)[0]
                off = begin - text[2]
                j = bisect.bisect_right(starts, off) - 1
                obj = text_syms[j][1] if j >= 0 else "?"
                counts[group_of(obj)] += 1
            print("## .pdata: %d entries (%d bytes)" % (n, n * 12))
            print()
            print("| group | entries | bytes |")
            print("|---|---:|---:|")
            for g, c in counts.most_common():
                print("| %s | %d | %d |" % (g, c, 12 * c))
            print()


if __name__ == "__main__":
    main()
