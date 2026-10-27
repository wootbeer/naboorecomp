#!/usr/bin/env python3
"""
Build N64Recomp's symbol file (NabooSyms/naboo.us.rev0.syms.toml) from a
splat/spimdisasm split of the ROM (tools/splat.yaml).

Each code segment's .s file gives one [[section]]: every `glabel` starts a
function, its size runs to its `endlabel`. Address ranges listed in
EXCLUDE are left out (data inside .text, such as the RSP microcode), and
SIZE_OVERRIDES fix functions that the scan ran into such a range.

    python3 tools/splat_to_syms.py <splat dir> -o NabooSyms/naboo.us.rev0.syms.toml
"""
import argparse
import os
import re
import sys

import rabbitizer  # installed with splat

# name, rom, vram, size of the code, .s file (relative to <splat dir>/asm).
# The section in the symbol file also covers the data after the code
# (DATA_SIZES), where the jump tables are.
SECTIONS = [
    (".main", 0x1000, 0x40000400, 0x4B2D0, "1000.s"),
    (".paged", 0x6F370, 0x41000000, 0xDA330, "6F370.s"),
]
DATA_SIZES = {".main": 0x6E370, ".paged": 0xDA390}
DATA_SIZES_BY_ROM = {rom: DATA_SIZES[name] for name, rom, _, _, _ in SECTIONS}

# [start, end) vram ranges that are not CPU code.
EXCLUDE = [
    (0x40001080, 0x40001150),  # RSP boot microcode (0xD0 bytes), STATUS.md round 4
    # From 0x4003A8C0: 0x4003A8B0 is the delay slot of func_4003A880's jr (STATUS.md round 13).
    (0x4003A8C0, 0x4003E630),  # RSP microcode (graphics/audio), STATUS.md round 3
    # Tables inside the hand-written routines at the end of .paged, read through
    # the address of the code (bltzal $zero), STATUS.md round 4.
    (0x410D9E1C, 0x410D9E24),
    (0x410D9ED8, 0x410D9EE0),
    (0x410DA29C, 0x410DA330),
]

# Functions outside the code the scan covers: name, vram, size.
EXTRA_FUNCS = [
    ("func_4004E880", 0x4004E880, 0x18),  # in .data: allocates cache lines over a range (stubbed)
]

# Function starts nothing in the scan points at.
EXTRA_STARTS = [
    0x4003A4DC,  # main(), reached from the boot stub through the TLB-mapped $t0
]

SIZE_OVERRIDES = {
}

# Names from tools/match_signatures.py (checked by hand, see syms/libultra.txt):
# "vram name size" per line. A match shorter than the scanned function splits it.
LIBULTRA = os.path.join(os.path.dirname(__file__), "..", "syms", "libultra.txt")


def load_names():
    names = {}
    if os.path.exists(LIBULTRA):
        for line in open(LIBULTRA):
            line = line.split("#")[0].split()
            if len(line) >= 3:
                names[int(line[0], 16)] = (line[1], int(line[2], 16))
    return names


def apply_names(funcs, names, rom, rom_base, vram_base, extra_starts=()):
    """Renames matched functions and splits a scanned function where a match ends early
    (or a match starts inside it), or where another function start is referenced."""
    starts = {v for _, v, _ in funcs} | set(names) | set(extra_starts)
    ends = {v + s for v, (_, s) in names.items()}
    out = []
    for name, vram, size in funcs:
        cuts = sorted(a for a in (starts | ends) if vram < a < vram + size)
        pieces = [vram] + cuts
        for i, start in enumerate(pieces):
            end = pieces[i + 1] if i + 1 < len(pieces) else vram + size
            # Skip nop padding after a function.
            while start < end and start not in names and rom[rom_base + start - vram_base:rom_base + start - vram_base + 4] == b"\0\0\0\0":
                start += 4
            if start >= end:
                continue
            if start in names:
                out.append((names[start][0], start, end - start))
            elif i == 0:
                out.append((name, start, end - start))
            else:
                out.append((f"func_{start:08X}", start, end - start))
    return out

def referenced_starts(rom, splat_dir):
    """Function starts the scan missed: every jal target, code addresses built
    with lui/addiu (thread entry points, callbacks), and code addresses stored in
    data that sit right after the end of a function (a `jr $ra` or `j` and its
    delay slot, or nop padding). Jump-table targets are not function starts."""
    # Entries of the jump tables spimdisasm found (jtbl_ labels in the data .s files).
    jlabels = set()
    data_dir = os.path.join(splat_dir, "asm", "data")
    for fn in os.listdir(data_dir):
        in_jtbl = False
        for line in open(os.path.join(data_dir, fn)):
            if line.startswith("dlabel "):
                in_jtbl = line.split()[1].startswith("jtbl_")
            elif in_jtbl:
                m = re.search(r"\.word (0x[0-9A-Fa-f]+|\.L[0-9A-F]+)", line)
                if m:
                    jlabels.add(int(m.group(1).replace(".L", "0x"), 16))
                elif line.startswith("enddlabel"):
                    in_jtbl = False

    # Jump tables of the paged code are in the main segment's data, where
    # spimdisasm doesn't label them: find each `jr $reg` that reads its target
    # from a table built by lui/addiu, sized by the sltiu bound check before it.
    for _, rom_base, vram, size, _ in SECTIONS:
        ws = [int.from_bytes(rom[o:o + 4], "big") for o in range(rom_base, rom_base + size, 4)]
        for i, w in enumerate(ws):
            if (w & 0xFC1FFFFF) != 0x00000008 or (w >> 21) & 31 == 31:
                continue
            regs = {(w >> 21) & 31}
            hi = lo = count = None
            for j in range(i - 1, max(i - 20, -1), -1):
                x = ws[j]
                op, rs, rt, rd, imm = x >> 26, (x >> 21) & 31, (x >> 16) & 31, (x >> 11) & 31, x & 0xFFFF
                if op == 0x23 and rt in regs and lo is None:  # lw reg, imm(base)
                    lo = (imm ^ 0x8000) - 0x8000
                    regs = {rs}
                elif op == 0 and (x & 0x3F) == 0x21 and rd in regs and lo is not None and hi is None:  # addu: base + index
                    regs = {rs, rt}
                elif op == 0x09 and rt in regs and lo is not None and hi is None:  # addiu
                    lo += (imm ^ 0x8000) - 0x8000
                    regs = {rs}
                elif op == 0x0F and rt in regs and lo is not None and hi is None:  # lui
                    hi = imm << 16
                elif op == 0x0B and count is None:  # sltiu
                    count = imm
            if hi is None or lo is None or count is None:
                continue
            table = (hi + lo) & 0xFFFFFFFF
            for k in range(count):
                for _, rb, vb, _, _ in SECTIONS:
                    if vb <= table + 4 * k < vb + DATA_SIZES_BY_ROM[rb]:
                        o = rb + table + 4 * k - vb
                        jlabels.add(int.from_bytes(rom[o:o + 4], "big"))

    def text_section(v):
        for _, rom_base, vram, size, _ in SECTIONS:
            if vram <= v < vram + size and not excluded(v):
                return rom_base, vram
        return None

    def word(v):
        sec = text_section(v)
        if sec is None:
            return None
        o = sec[0] + v - sec[1]
        return int.from_bytes(rom[o:o + 4], "big")

    starts = set(EXTRA_STARTS)
    for _, rom_base, vram, size, _ in SECTIONS:
        for v in range(vram, vram + size, 4):
            if excluded(v):
                continue
            w = word(v)
            if w >> 26 == 3:  # jal
                t = ((v + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
                if text_section(t):
                    starts.add(t)
    # Code addresses built by lui/addiu (thread entry points, callbacks).
    for _, rom_base, vram, size, _ in SECTIONS:
        for v in range(vram, vram + size, 4):
            if excluded(v):
                continue
            w = word(v)
            if w is None or w >> 26 != 0x0F:
                continue
            rt, hi = (w >> 16) & 31, (w & 0xFFFF) << 16
            for k in range(1, 9):
                x = word(v + 4 * k)
                if x is None:
                    break
                if x >> 26 == 0x09 and (x >> 21) & 31 == rt:
                    t = (hi + ((x & 0xFFFF) ^ 0x8000) - 0x8000) & 0xFFFFFFFF
                    if text_section(t) and t not in jlabels and t % 4 == 0:
                        starts.add(t)
                    break
                if (x >> 16) & 31 == rt and x >> 26 not in (0x2B, 0x28, 0x29, 0x04, 0x05):
                    break  # rt overwritten by something else
    # Code pointers in the main segment's data.
    for o in range(0x4C2D0, 0x6F370, 4):
        t = int.from_bytes(rom[o:o + 4], "big")
        if t % 4 or not text_section(t) or t in jlabels:
            continue
        before = word(t - 8)
        if before == 0x03E00008 or (before is not None and before >> 26 == 2) or word(t - 4) == 0:
            starts.add(t)
    return starts - jlabels, jlabels


INSN_RE = re.compile(r"/\* ([0-9A-F]+) ([0-9A-F]{8}) [0-9A-F]{8} \*/")


def parse(path):
    funcs = []
    cur = None
    last_vram = None
    with open(path) as f:
        for line in f:
            if line.startswith("glabel "):
                cur = [line.split()[1], None, None]
            elif line.startswith("endlabel ") and cur is not None:
                cur[2] = last_vram + 4 - cur[1]
                funcs.append(tuple(cur))
                cur = None
            else:
                m = INSN_RE.search(line)
                if m:
                    last_vram = int(m.group(2), 16)
                    if cur is not None and cur[1] is None:
                        cur[1] = last_vram
    return funcs


def excluded(vram):
    return any(a <= vram < b for a, b in EXCLUDE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("splat_dir")
    ap.add_argument("rom")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()
    rom_data = open(args.rom, "rb").read()
    names = load_names()
    extra, jump_targets = referenced_starts(rom_data, args.splat_dir)

    out = [
        "# Battle for Naboo (USA) symbol file for N64Recomp.",
        "# Generated by tools/splat_to_syms.py from a splat/spimdisasm split",
        "# (tools/splat.yaml) and the libultra names in syms/libultra.txt; hand fixes go",
        "# in that script or that list so they survive a regeneration.",
        "# .main is linked at 0x40000400 (TLB-mapped onto RDRAM from physical 0x400);",
        "# .paged at 0x41000000 is demand-paged from ROM by the game's TLB-miss handler.",
        "# See STATUS.md rounds 1-3.",
        "",
    ]
    for name, rom, vram, size, asm in SECTIONS:
        funcs = parse(os.path.join(args.splat_dir, "asm", asm))
        funcs = [(n, v, SIZE_OVERRIDES.get(n, s)) for n, v, s in funcs if not excluded(v)]
        # The scan starts a function after every `jr $ra`; one that starts at a
        # jump-table target is a case of the function before it.
        merged = []
        for n, v, sz in funcs:
            if merged and v in jump_targets and v not in names:
                pn, pv, psz = merged[-1]
                merged[-1] = (pn, pv, v + sz - pv)
            else:
                merged.append((n, v, sz))
        funcs = merged
        funcs = apply_names(funcs, names, rom_data, rom, vram, extra)
        # Drop pieces inside excluded ranges, and end functions where one starts.
        clipped = []
        for n, v, sz in funcs:
            if excluded(v):
                continue
            for a, b in EXCLUDE:
                if v < a < v + sz:
                    sz = a - v
            clipped.append((n, v, SIZE_OVERRIDES.get(n, sz)))
        funcs = clipped
        # Data the scan took for code (strings, float constants, tables) either
        # doesn't decode or calls outside the code: a function ends before the
        # first such word (and any nops before it), a piece that starts with one
        # is dropped.
        def is_code(w, at):
            if not rabbitizer.Instruction(w, at).isValid():
                return False
            if w >> 26 == 3:
                t = ((at + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
                return any(sv <= t < sv + DATA_SIZES[sn] for sn, _, sv, _, _ in SECTIONS)
            return True

        valid = []
        for n, v, sz in funcs:
            o = rom + v - vram
            words = [int.from_bytes(rom_data[o + i:o + i + 4], "big") for i in range(0, sz, 4)]
            bad = next((i for i, w in enumerate(words) if not is_code(w, v + i * 4)), None)
            if bad is not None:
                end = bad
                while end > 0 and words[end - 1] == 0:
                    end -= 1
                if end == 0:
                    print(f"dropped {n} (0x{v:08X}): not code", file=sys.stderr)
                    continue
                # What's left has to leave somewhere (jr or j), or it was data too.
                if not any(w >> 26 == 2 or (w & 0xFC1FFFFF) == 0x00000008 for w in words[:end]):
                    print(f"dropped {n} (0x{v:08X}): not code", file=sys.stderr)
                    continue
                print(f"trimmed {n} (0x{v:08X}) to 0x{end * 4:X}: data at 0x{v + bad * 4:08X}", file=sys.stderr)
                sz = end * 4
            valid.append((n, v, sz))
        funcs = valid
        funcs += [f for f in EXTRA_FUNCS if vram <= f[1] < vram + DATA_SIZES[name]]
        funcs.sort(key=lambda f: f[1])
        out += ["[[section]]", f'name = "{name}"', f"rom = 0x{rom:08X}",
                f"vram = 0x{vram:08X}", f"size = 0x{DATA_SIZES[name]:X}", "", "functions = ["]
        for fname, fvram, fsize in funcs:
            out.append(f'    {{ name = "{fname}", vram = 0x{fvram:08X}, size = 0x{fsize:X} }},')
        out += ["]", ""]
    with open(args.output, "w") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    main()
