# patches/

C code compiled for the N64 (MIPS) and recompiled alongside the game, for
changes that replace a whole game function. A `RECOMP_PATCH` function here
takes the place of the original function of the same name. None yet: the
headers, linker scripts and Makefile are ready for the first one (from the
BattleTanx: Global Assault port, where this hooks the frontend's per-frame UI
pump into the game's VI swap).

The build is automatic once there is a `.c` file here: `make` produces
`patches.elf` (through WSL on Windows, since the Windows clang builds have no
MIPS backend), then N64Recomp recompiles it using a `patches.toml` in the repo
root (not added yet). `syms.ld` gives native runtime functions dummy addresses
so the patches can call them.

Smaller fixes don't live here: instruction patches and hooks that run native
code at a point in a game function are in `naboo.us.rev0.toml`, with the
native code in `src/game/`.
