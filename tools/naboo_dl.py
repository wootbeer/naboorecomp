#!/usr/bin/env python3
"""
Walk a Battle for Naboo display list from a RAM capture (src/game/gfx_dump.cpp)
and print or count its commands, to study Factor 5's graphics microcode.

    python3 tools/naboo_dl.py gfx_30.rdram 0x80659108 [--print] [--max N]

Layout (STATUS.md rounds 11-13): a display list pointer points at a chunk
header, `[next chunk, previous chunk]`; commands follow it and run for 0x100
bytes, then continue at the next chunk's header (B5 jumps there early). 06
calls a list, 07 branches to one, B8 returns. Command names are the F3D ones
where the opcode matches, plus the Factor 5 ones. Lengths of the multi-word
commands (05 geometry generation, B4/BF triangles) as GLideN64's
F5Indi_Naboo.cpp handles them; 05/0F's length depends on a DMEM flag the
walker can't see, so it is shown as 16 bytes.
"""
import argparse
import collections
import struct

NAMES = {
    0x00: "NOOP", 0x01: "F5_MOVEMEM", 0x02: "F5_SET_DLIST_ADDR", 0x05: "F5_GEOMETRY_GEN",
    0x06: "DL", 0x07: "BRANCH_DL", 0xB4: "F5_TRI2", 0xB5: "F5_JUMPDL", 0xB6: "CLEARGEOMETRYMODE",
    0xB7: "SETGEOMETRYMODE", 0xB8: "ENDDL", 0xB9: "SETOTHERMODE_L", 0xBA: "SETOTHERMODE_H",
    0xBB: "TEXTURE", 0xBC: "MOVEWORD", 0xBD: "POPMTX?", 0xBE: "F5_BE", 0xBF: "F5_TRI1",
    0xE4: "F5_TEXRECT", 0xE6: "RDPLOADSYNC", 0xE7: "RDPPIPESYNC", 0xE8: "RDPTILESYNC",
    0xE9: "RDPFULLSYNC", 0xF0: "LOADTLUT", 0xF2: "SETTILESIZE", 0xF3: "LOADBLOCK",
    0xF4: "LOADTILE", 0xF5: "SETTILE", 0xF6: "FILLRECT", 0xF7: "SETFILLCOLOR",
    0xF8: "SETFOGCOLOR", 0xF9: "SETBLENDCOLOR", 0xFA: "SETPRIMCOLOR", 0xFB: "SETENVCOLOR",
    0xFC: "SETCOMBINE", 0xFD: "SETTIMG", 0xFE: "SETZIMG", 0xFF: "SETCIMG",
}


class Ram:
    def __init__(self, path):
        self.d = open(path, "rb").read()

    def word(self, addr):
        a = addr & 0x1FFFFFFF
        return struct.unpack(">I", self.d[a:a + 4])[0]


GEN_LENGTHS = {0x09: 24, 0x0C: 24, 0x0F: 16, 0x15: 32, 0x24: 40, 0x27: 24, 0x4F: 8}


def length(op, w0, w1):
    if op == 0x05:
        return GEN_LENGTHS.get(w1 & 0xFF, 8)
    if op in (0xB4, 0xBF):
        return 32 if w0 & 0x200 else 16
    return 8


def walk(ram, start, on_cmd, max_cmds=200000):
    stack = []
    chunk = start
    pos = chunk + 8
    count = 0
    while count < max_cmds:
        if pos >= chunk + 8 + 0x100:
            nxt = ram.word(chunk)
            if nxt == 0:
                break
            chunk, pos = nxt, nxt + 8
            continue
        w0, w1 = ram.word(pos), ram.word(pos + 4)
        op = w0 >> 24
        n = length(op, w0, w1)
        on_cmd(pos, op, w0, w1, len(stack), [ram.word(pos + i) for i in range(8, n, 4)])
        count += 1
        pos += n
        if op == 0xB5:
            nxt = ram.word(chunk)
            if nxt == 0:
                break
            chunk, pos = nxt, nxt + 8
        elif op == 0x06:
            stack.append((chunk, pos))
            chunk, pos = w1, w1 + 8
        elif op == 0x07:
            chunk, pos = w1, w1 + 8
        elif op == 0xB8:
            if not stack:
                break
            chunk, pos = stack.pop()
    return count


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rdram")
    ap.add_argument("start", type=lambda s: int(s, 16))
    ap.add_argument("--print", action="store_true")
    ap.add_argument("--max", type=int, default=200000)
    args = ap.parse_args()
    ram = Ram(args.rdram)
    hist = collections.Counter()

    def on_cmd(pos, op, w0, w1, depth, extra):
        key = (op, w1 & 0xFF) if op == 0x05 else (op, None)
        hist[key] += 1
        if args.print:
            more = " ".join(f"{x:08X}" for x in extra)
            print(f"{pos:08X} {'  ' * depth}{w0:08X} {w1:08X} {more:<24} {NAMES.get(op, f'op_{op:02X}')}")

    n = walk(ram, args.start, on_cmd, args.max)
    print(f"{n} commands")
    for (op, sub), c in sorted(hist.items(), key=lambda kv: -kv[1]):
        name = NAMES.get(op, "?") + (f"/{sub:02X}" if sub is not None else "")
        print(f"  {op:02X} {name:<22} {c}")


if __name__ == "__main__":
    main()
