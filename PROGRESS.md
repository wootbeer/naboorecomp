# Progress / Roadmap

Where the port stands. STATUS.md has the full history of how each piece was
found and fixed.

The goal is the original game, running natively: fixes are limited to what
the PC port needs (timing, rendering and runtime differences from the N64),
and leave the game's own behaviour, look and balance alone. Same approach and
tooling as the BattleTanx: Global Assault port.

## Working

- **Recompilation**: both code segments (`.main` at `0x40000400`, `.paged` at
  `0x41000000`, about 1.2 MB of MIPS code, 2,333 functions) recompile with no
  errors. The symbol file is generated from a splat split
  (`tools/splat_to_syms.py`), with libultra named from n64sym's signatures.
- **The game's TLB use**: the code runs at `0x40000000`/`0x41000000` through
  the TLB, with the second segment paged in from ROM on demand on the N64.
  The port reduces every address to its physical offset (patches to
  N64Recomp's `recomp.h` and the runtime) and loads the paged segment once at
  boot.
- **Boot**: the launcher with the ROM picker (and the hash check for this
  release), then the game's own boot: `osInitialize` (with the clock-rate
  scaling the BattleTanx port had to find in its round 138), the memory
  setup, all of the game's threads, VI events, the video mode, the game's
  file loader, and on into its main loop (graphics and audio tasks, frame
  swaps, controller reads). The screen stays black: see Graphics below.
- **Audio microcode**: MusyX located and run through RSPRecomp (first pass).

## Not working yet

- **Graphics**: the game draws with Factor 5's own RSP graphics microcode,
  which stock RT64 doesn't understand. Rogue Squadron's recomp has a fork of
  RT64 for Factor 5's microcode; Battle for Naboo uses a later version of it,
  so this is the main piece of work before anything shows on screen.
- **Audio**: MusyX's code is longer than the RSP's instruction memory and
  wraps around; the RSPRecomp config needs that layout and its jump targets.
- **Saves**: EEPROM through the runtime (AllowAll, 16 Kbit); not tried yet.

## Planned from the start (lessons from the earlier ports)

- **High framerates**: interpolation needs stable matrix IDs from the start,
  and the game's frame-time math checked for the truncation problems the
  BattleTanx port had (`(int)step` going to 0). The game runs a variable
  timestep, so frame pacing and the clock rate matter more here.
- **Widescreen**: the 3D view and culling to extend with the aspect ratio,
  the HUD anchored to the screen edges, nothing important pushed off screen.

## Next

1. Factor 5 graphics microcode in RT64. References: GLideN64's
   implementation of the Indiana Jones / Battle for Naboo microcode (issue
   #1259; GPL-2.0, so for how the commands work, not for code) and the
   Rogue Squadron RT64 fork (an earlier version of the microcode).
2. MusyX audio microcode layout.
3. The in-game quit prompt and UI (the per-frame UI pump).
4. Title screen, menus and the first mission.

## Where things are

| Path | What |
|---|---|
| `naboo.us.rev0.toml` | N64Recomp config: stubs, instruction patches, hooks |
| `NabooSyms/` | Symbol file (generated, see its README) |
| `musyx.us.rev0.toml` | RSP audio microcode config (MusyX) |
| `syms/rom_info.md` | ROM identification and memory layout |
| `syms/libultra.txt` | libultra names and where they are |
| `src/main/` | Frontend: window, audio, input, config, crash reporting |
| `src/game/` | Game-specific code: paged segment, OS setup, runtime shims |
| `patches/` | Whole-function replacements compiled for MIPS (none yet) |
| `assets/` | UI font, icons and their licenses |
| `icons/` | App icon (`tools/make_app_icon.py`) |
| `lib-patches/` | Patches applied to submodules at configure time |
| `tools/` | ROM splitting, symbol generation, analysis, release packaging |
