# Star Wars Episode I: Battle for Naboo (2000 N64) Recompiled and Port

You will need the game file from a licensed copy in order to play it.

A native PC port of Star Wars Episode I: Battle for Naboo, made by statically
recompiling the N64 game with [N64Recomp](https://github.com/N64Recomp/N64Recomp)
and running it on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
with [RT64](https://github.com/rt64/rt64) for rendering. Built the same way as
the BattleTanx: Global Assault port.

Unofficial, and not affiliated with the rights holders. **No game data is
included.** You need your own dump of the game to build or play this.

**Status: playable, work in progress.** The game boots, its menus and
missions play through, with widescreen, sound, saves and controller support.
There are known rough edges (see [PROGRESS.md](PROGRESS.md)), and there are
no releases yet: to play, build it yourself ([BUILDING.md](BUILDING.md)).

`main` follows the work as it happens; [STATUS.md](STATUS.md) is the
detailed log of every round.

## Game Files
You need your own data files: Star Wars Episode I - Battle for Naboo (USA).n64
(`.z64`, `.n64` and `.v64` all work). The game asks for it on first launch.

SHA-1 of the dump (as `.n64`, byte-swapped):
9F2782C314F7A8F481E6515FCD19C5431C6D5347
SHA-1 once normalized to `.z64`:
E4441A6EEB67861408C2E009BAAE8AAD4DF34021

## Where settings and saves go
New installs are portable: settings, saves and mods are kept in the game's
folder (the folder it's started from), marked by a `portable.txt` the game
creates there on its first run. If you already have them in the per-user
folder (`%LOCALAPPDATA%\naboorecomp` on Windows, `~/.config/naboorecomp` on
Linux), they stay there. To switch, create or delete `portable.txt`.

## Command line
`NabooRecompiled --help` lists every option. The main ones:

- `--rom <file>` uses this ROM (checked and stored, like Select ROM).
- `--play` goes straight into the game, skipping the launcher.
- Any setting from the General, Graphics or Sound tab:
  `--set graphics.msaa_option=MSAA4X`, or a short name such as
  `--fullscreen`, `--msaa`, `--resolution`, `--framerate`, `--volume`.
  `--list-settings` shows them all with the values they take. They apply to
  that launch; add `--save-settings` to keep them.
- `--portable`, `--no-portable`, `--data-dir <folder>` choose where data goes.
- Game switches: `--no-frame-interpolation`, `--perf`, `--dump-dir <folder>`.

For example:
`NabooRecompiled --rom "Battle for Naboo (USA).z64" --play --fullscreen --framerate 120`

## Building
See [BUILDING.md](BUILDING.md).

## Issues and Limitations

- See [PROGRESS.md](PROGRESS.md) for what's known not to work yet.
- Please see the "Issues" section in GitHub.

## Credits
- [N64Recomp and N64ModernRuntime](https://github.com/N64Recomp) by Mr-Wiseguy
  and contributors, which this port is built with
- [RT64](https://github.com/rt64/rt64) by Dario and contributors, the renderer
- [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) by the N64Recomp
  contributors, the launcher and input layer
- [splat](https://github.com/ethteck/splat) and its N64 tooling
  (`spimdisasm`, `rabbitizer`), used to split the ROM (`tools/splat.yaml`)
- [n64sym](https://github.com/shygoo/n64sym) by shygoo, whose libultra
  signatures name the library functions (`tools/match_signatures.py`)
- olivieryuyu's decoding of Factor 5's Indiana Jones / Battle for Naboo
  graphics microcode for [GLideN64](https://github.com/gonetz/GLideN64)
  (`F5Indi_Naboo.cpp`), which the RT64 support here follows as a description
  of how the commands behave; no GLideN64 code is included
- [mupen64plus](https://mupen64plus.org/), used as a reference to compare the
  game's memory against (`tools/mupen_dump.c`, `tools/mupen_watch.c`)
- The Rogue Squadron projects, Battle for Naboo's predecessor on the same
  Factor 5 engine, used as a reference for Factor 5's microcode and engine:
  [RogueSquadron64Recomp](https://github.com/MikeSemicolonD/RogueSquadron64Recomp)
  by MikeSemicolonD and the
  [rogue_squadron64](https://github.com/Tmcg2/rogue_squadron64) decomp by Tmcg2.
  No code from them is included in this repository.

## License
The project's own code is GPL-3.0; see [COPYING](COPYING). The game itself remains
the property of its rights holders.
