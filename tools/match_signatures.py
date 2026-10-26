#!/usr/bin/env python3
"""
Name libultra (and other SDK) functions in the ROM by matching them against
an n64sym signature file (https://github.com/shygoo/n64sym, MIT;
src/builtin_signatures.sig), the same matching n64sym does, in plain Python.

A signature is the CRC32 of a function's bytes with every relocated field
(the low 16 bits for hi16/lo16, the low 26 bits for a jal target) zeroed.
Candidates are the function starts already in the symbol file.

    python3 tools/match_signatures.py ROM.z64 NabooSyms/naboo.us.rev0.syms.toml \
        builtin_signatures.sig > syms/libultra_matches.txt

Output: one "vram name size" line per match, ambiguous matches (several names
for the same bytes) on one line separated by '|'.
"""
import re
import sys
import zlib
from collections import defaultdict

RELOC_MASK = {".hi16": 0xFFFF0000, ".lo16": 0xFFFF0000, ".targ26": 0xFC000000}


def load_sigs(path):
    sigs = []
    cur = None
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p or p[0].startswith("#"):
                continue
            if p[0].startswith("."):
                for off in p[2:]:
                    cur["relocs"][int(off, 16)] = RELOC_MASK[p[0]]
            else:
                cur = {"name": p[0], "size": int(p[1], 16), "crcA": int(p[2], 16),
                       "crcB": int(p[3], 16), "relocs": {}}
                sigs.append(cur)
    return sigs


def stripped(data, relocs):
    if not relocs:
        return data
    b = bytearray(data)
    for off, mask in relocs.items():
        if off + 4 <= len(b):
            w = int.from_bytes(b[off:off + 4], "big") & mask
            b[off:off + 4] = w.to_bytes(4, "big")
    return bytes(b)


def main():
    rom = open(sys.argv[1], "rb").read()
    text = open(sys.argv[2]).read()
    sigs = load_sigs(sys.argv[3])

    # (rom, vram) of each candidate function start.
    cands = []
    for sec in re.finditer(r"rom = (0x[0-9A-F]+)\s*vram = (0x[0-9A-F]+).*?functions = \[(.*?)\]", text, re.S):
        rom_base, vram_base = int(sec.group(1), 16), int(sec.group(2), 16)
        for m in re.finditer(r"vram = (0x[0-9A-F]+)", sec.group(3)):
            v = int(m.group(1), 16)
            cands.append((rom_base + v - vram_base, v))

    # Group by the relocations inside the first 8 bytes, so crcA is computed once per pattern.
    groups = defaultdict(list)
    for s in sigs:
        key = tuple(sorted((o, m) for o, m in s["relocs"].items() if o < 8))
        groups[(key, min(s["size"], 8))].append(s)

    found = defaultdict(list)
    for (key, n), group in groups.items():
        by_crc = defaultdict(list)
        for s in group:
            by_crc[s["crcA"]].append(s)
        relocs = dict(key)
        for rom_off, vram in cands:
            head = stripped(rom[rom_off:rom_off + n], relocs)
            for s in by_crc.get(zlib.crc32(head), ()):
                body = stripped(rom[rom_off:rom_off + s["size"]], s["relocs"])
                if zlib.crc32(body) == s["crcB"]:
                    found[vram].append((s["name"], s["size"]))

    for vram in sorted(found):
        names = sorted({n for n, _ in found[vram]})
        size = max(sz for _, sz in found[vram])
        print(f"0x{vram:08X} {'|'.join(names)} 0x{size:X}")


if __name__ == "__main__":
    main()
