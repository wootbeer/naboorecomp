"""Disassemble Factor 5's graphics microcode (or any RSP code) from the ROM.

    python3 tools/rsp_disasm.py game.z64 ROM_OFFSET SIZE IMEM_ADDRESS

Prints one instruction per line, IMEM addresses as the RSP sees them
(e.g. 0x04001000 + offset). Used to study the terrain commands (STATUS.md
round 17).
"""
import sys

import rabbitizer


def main():
    rom = open(sys.argv[1], "rb").read()
    off, size, base = (int(a, 0) for a in sys.argv[2:5])
    for i in range(0, size, 4):
        word = int.from_bytes(rom[off + i:off + i + 4], "big")
        ins = rabbitizer.Instruction(word, vram=base + i, category=rabbitizer.InstrCategory.RSP)
        print(f"{base + i:08X}: {word:08X}  {ins.disassemble()}")


if __name__ == "__main__":
    main()
