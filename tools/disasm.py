#!/usr/bin/env python3
"""
Disassemble Battle for Naboo code by its game address, either segment.

    python3 tools/disasm.py "Battle for Naboo (USA).z64" 0x4003A4DC [count]

Addresses are mapped to the ROM the way the game links them: .main at
0x40000400 from ROM 0x1000, .paged at 0x41000000 from ROM 0x6F370
(syms/rom_info.md). Needs rabbitizer (pip install rabbitizer; splat
installs it too).
"""
import struct
import sys

import rabbitizer

SEGMENTS = [(0x40000400, 0x1000, 0x6E370), (0x41000000, 0x6F370, 0xDA390)]


def rom_offset(vram):
    for base, rom, size in SEGMENTS:
        if base <= vram < base + size:
            return rom + vram - base
    raise SystemExit(f"0x{vram:08X} is not in a code segment")


def main():
    rom = open(sys.argv[1], "rb").read()
    vram = int(sys.argv[2], 16)
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 32
    off = rom_offset(vram)
    for i in range(count):
        word = struct.unpack(">I", rom[off + i * 4:off + i * 4 + 4])[0]
        ins = rabbitizer.Instruction(word, vram + i * 4)
        print(f"{off + i * 4:08X} {vram + i * 4:08X} {word:08X}  {ins.disassemble()}")


if __name__ == "__main__":
    main()
