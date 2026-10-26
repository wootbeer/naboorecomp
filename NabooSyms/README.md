`naboo.us.rev0.syms.toml` is N64Recomp's symbol file for this game: the
function boundaries (vram, size and name) of its two code segments, `.main`
(`0x40000400`) and `.paged` (`0x41000000`), see `syms/rom_info.md`.
`naboo.us.rev0.toml` in the repo root points N64Recomp at it.

It is generated, not edited by hand: `tools/splat_to_syms.py` builds it from a
splat/spimdisasm split of the ROM (`tools/splat.yaml`) and the libultra names
in `syms/libultra.txt` (found with `tools/match_signatures.py`). The script
also splits functions the scan merged (at every `jal` target, code address
built with `lui`/`addiu`, and code pointer in data), merges cases of a
`switch` back into their function (jump-table targets), and leaves out data
inside the code (the RSP microcode, tables, strings). Fixes go in the script
or `syms/libultra.txt` so a regeneration keeps them.
