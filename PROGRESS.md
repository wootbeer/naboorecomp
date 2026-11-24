# Progress / Roadmap

Where the port stands. STATUS.md has the full history of how each piece was
found and fixed.

The goal is the original game, running natively: fixes are limited to what
the PC port needs (timing, rendering and runtime differences from the N64),
and leave the game's own behaviour, look and balance alone. Same approach and
tooling as the BattleTanx: Global Assault port.

## Working

- **The whole game boots and plays**: launcher and ROM check, intro, menus,
  craft select, and missions through to their end screens. Both code
  segments (about 1.2 MB of MIPS code, 2,333 functions) recompile cleanly.
- **Graphics**: RT64 runs Factor 5's own graphics microcode in an RSP
  interpreter, so the terrain, sky, particles and text it generates all
  draw. Models go through RT64 in 3D.
- **Widescreen**: the 3D view, the city levels' portal culling and the open
  levels' terrain are widened with the window; the HUD is anchored to the
  sides; snow and the full-screen wipes cover the wide picture. Cutscenes
  stay 4:3 (their sets are only built as far as the 4:3 camera sees).
- **Audio**: MusyX through RSPRecomp, with headroom and a limiter so loud
  scenes don't crackle.
- **Saves**: the cartridge's EEPROM, and Controller Paks as `.mpk` files;
  Rumble Pak; local multiplayer.
- **Speed**: loads about 6x faster than first, and the game's threads no
  longer spin a CPU core while they wait.
- **Portable by default** for new installs, and command-line options for
  every setting, the ROM and going straight into the game (`--help`).

## Known issues

- **Frame rate**: the game runs at its own 30 fps. RT64's generated frames
  in between show the game's frames as made; moving objects smoothly in
  between (the experimental mode on F10) isn't good enough yet. This
  engine gives RT64 one finished matrix per object, so objects have to be
  identified from the game's own scene data; that's the work in progress.
- Objects reported as culled at the very edges of a wide screen (not yet
  reproduced).
- Distant objects can appear mid-screen (likely the game's own draw
  distance; to compare with an emulator).
- Menus after quitting a mission reported slow on one machine (not
  reproduced).
- The graphics microcode is interpreted: about 4-5 ms of CPU per frame.

## Next

1. Frame interpolation built on the game's own object identities.
2. The open reports above, from captures (F12 saves a frame to replay).

## Where things are

| Path | What |
|---|---|
| `naboo.us.rev0.toml` | N64Recomp config: stubs, instruction patches, hooks |
| `NabooSyms/` | Symbol file (generated, see its README) |
| `musyx.us.rev0.toml` | RSP audio microcode config (MusyX) |
| `syms/rom_info.md` | ROM identification and memory layout |
| `syms/libultra.txt` | libultra names and where they are |
| `src/main/` | Frontend: window, audio, input, config, crash reporting |
| `src/game/` | Game-specific code: paged segment, OS setup, widescreen, audio, hooks |
| `patches/` | Whole-function replacements compiled for MIPS (none yet) |
| `assets/` | UI font, icons and their licenses |
| `icons/` | App icon (`tools/make_app_icon.py`) |
| `lib-patches/` | Patches applied to submodules at configure time (RT64's Factor 5 support is `lib-patches/rt64/`) |
| `tools/` | ROM splitting, symbol generation, analysis, release packaging |
