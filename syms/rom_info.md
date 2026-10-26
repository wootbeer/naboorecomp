# ROM identification and layout

Source dump: `Star Wars Episode I - Battle for Naboo (USA).n64` (despite the
`.n64` extension, **v64 / byte-swapped** data). Normalize with
`tools/normalize_rom.py` before feeding it to splat or N64Recomp, which expect
big-endian `.z64`.

| Field | Value |
|---|---|
| Internal name | `Battle for Naboo` |
| Game code | `NNAE` (region `E` = USA, version 0) |
| Entry point | `0x80000400` (linked at `0x40000400`, see below) |
| CRC1 / CRC2 | `0x3d02989b` / `0xd4a381e2` |
| Size | 33,554,432 bytes (32 MiB) |
| SHA-1 (.n64 as dumped) | `9f2782c314f7a8f481e6515fcd19c5431c6d5347` |
| SHA-1 (normalized .z64) | `e4441a6eeb67861408c2e009baae8aad4df34021` |
| XXH3-64 (normalized .z64) | `0xf1fef7568d4264fd` (the launcher's ROM check) |

## Code and the TLB (STATUS.md rounds 1-4)

The game runs its code through the CPU's TLB, not from KSEG0:

- The boot stub at ROM `0x1000` (IPL3 starts it at `0x80000400`) maps
  virtual `0x40000000` onto physical 0 with a pair of 4 MB pages, then jumps
  to `0x4000044C` and on to `main()` at `0x4003A4DC`.
- **`.main`**: ROM `0x1000`-`0x6F370`, linked at `0x40000400`. Code to
  `0x4004B6D0` (ROM `0x4C2D0`), then data; BSS `0x4006E770`-`0x400A74E0`.
  libultra is at `0x4003E630`-`0x4004B6D0` (`syms/libultra.txt`).
- **`.paged`**: ROM `0x6F370`-`0x1496A0` (+0x60 bytes of data), linked at
  `0x41000000`. Never loaded as a whole by default: a TLB-miss handler
  (`func_400008F4`) DMAs each 4 KB page from ROM into a page pool on first
  use. The memory setup (`func_40039960`) can instead map the whole segment
  onto the top of RAM with `osMapTLB` and DMA it there.
- RSP microcode inside `.main`'s code: Factor 5's RSP boot at `0x40001080`
  (0xD0 bytes), the graphics microcode at `0x4003A8C0` and MusyX audio at
  `0x4003D210`, up to `0x4003E630`.
- Assets start at ROM `0x1496E0` (a named file table: `dbg_data`, level and
  sound files), mostly zlib-compressed.

On the PC, all game addresses are reduced to their physical offset
(`addr & 0x1FFFFFFF`), so `0x40xxxxxx` and `0x80xxxxxx` reach the same RDRAM
and `0x41xxxxxx` reaches `0x01xxxxxx`, where the port preloads `.paged` from
ROM at boot (`src/game/paged_segment.cpp`).

## Saves

The game links libultra's EEPROM functions and saves to the cartridge
(it only checks that an EEPROM is present); it also links the Controller
Pak and Rumble Pak code.
