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
  file loader, and on into its title screen and first 3D scenes, matching
  the game on an emulator and without crashing (STATUS.md round 13).
- **Audio microcode**: MusyX located and run through RSPRecomp (first pass).

## Not working yet

- **Graphics**: RT64 now recognises Factor 5's graphics microcode and runs
  its display lists: 2D screens and 3D models show. The terrain and
  backgrounds the microcode generates itself (commands 05/BE) aren't drawn
  yet.
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

1. Graphics: the terrain and background commands (05/BE) of Factor 5's
   microcode.
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
