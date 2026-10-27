# Status

The project's working log: every investigation and fix, newest first, as
"rounds". PROGRESS.md has the short version of where things stand; this file
is for finding out why something is the way it is. Same format as the
BattleTanx: Global Assault port's STATUS.md, whose lessons are referred to as
"the BattleTanx port, round N".

## Overview (as of round 17)

**Where it stands.** The game's code recompiles cleanly and boots on Linux:
the launcher, the ROM check, osInitialize, the memory setup, all of the
game's threads, the VI and the video mode, its file loader, audio, and on
into its title screen and first 3D scenes, now running without crashing and
with the same game state as on an emulator (round 13). RT64 recognises
Factor 5's graphics microcode and runs its display lists (triangles,
matrices, lighting, text rectangles), and the splash screen and the first
3D scene's models show, and the intro plays through to the Factor 5 logo
(rounds 14-15). Since round 17 RT64 runs the microcode itself in an RSP
interpreter, so the terrain it generates shows too; drawing its triangles
in 3D (for widescreen and interpolation) is in progress.

**How the main areas got here** (the rounds to read for each):

- **ROM layout and the TLB** (rounds 1-2). The code runs through the TLB at
  `0x40000000` and `0x41000000`; the second segment is paged in from ROM.
- **Symbols** (rounds 3-4). splat/spimdisasm split, libultra named from
  n64sym's signatures, and a generator that fixes the split
  (`tools/splat_to_syms.py`).
- **Recompiling** (round 4). N64Recomp and RSPRecomp fixes in
  `lib-patches/N64Recomp/`, the config in `naboo.us.rev0.toml`.
- **Runtime** (rounds 5-7, 13). osInitialize's clock rate, the shims the
  game needs, osYieldThread, VI events before a video mode; thread queue and
  osStopThread fixes, osViSetMode clearing osViBlack.
- **Graphics** (rounds 10-17). Factor 5's microcode in RT64: HLE commands
  (`lib-patches/rt64/0023`), then the microcode itself in an RSP
  interpreter (`0024`, round 17).
- **Checking against an emulator** (round 13). `tools/mupen_dump.c` and
  `tools/mupen_watch.c` run the game in mupen64plus's core to compare RAM
  and find which code writes an address.

**Principle.** The game stays original: fixes cover only what differs from
the N64 (timing, rendering, the runtime), not the game's look or behaviour.

# Log

## 2026-10-27, round 17: running Factor 5's microcode itself

**What changed.** RT64 now runs the game's graphics microcode in an RSP
interpreter (`lib-patches/rt64/0024`) instead of reimplementing its
commands, and draws the RDP commands it produces. The terrain and the other
geometry the microcode generates (`05`, `BE`) come out as on the N64: the
snowy hills under the LucasArts intro show, and the logo's textures line up.
`RT64_NABOO_HLE=1` goes back to round 16's HLE commands.

**The interpreter** (`src/gbi/rt64_rsp_interpreter.{h,cpp}` in RT64).
Scalar unit written here; the vector unit is ares's, as adapted in
librecomp (`rt64_rsp_vu.h`, ISC licence, wrapped in a namespace). DMAs and
the RDP finish at once. The microcode's RDP command buffer is pointed past
the end of RDRAM, into memory of the interpreter's own, so nothing of the
game's is overwritten; each write to `DPC_END` hands the new commands to
RT64's `Interpreter::processRDPLists`. About 2-9 ms a frame on the intro.

**Checked against the reference.** The interpreter's RDP output for the
captured tasks 120, 600 and 1200 is byte-identical to mupen64plus's
low-level RSP (round 17's `tools/rsp_reference.c`). Task 900 differs in
three words, from z64 itself: fuzzing single vector instructions on both
showed z64's `VRSQ` returning 0 and its `VABS`/`VMOV` accumulators differing
from ares (which matches hardware); all the other instructions agree. Two
bugs were found on the way:

- librecomp's `RSP_MEM_B` reads DMEM bytes as signed, so `LUV`/`LHV`/`LFV`
  (`byte << 7`) sign-extend: shade colours above 0x7F came out negative.
  The RT64 copy reads them unsigned, as ares does. (Audio's microcode
  doesn't use those instructions.)
- The ares code relies on type punning through unions: with strict
  aliasing, GCC at -O2 dropped `VMOV`'s result. The file is built with
  `-fno-strict-aliasing` (and `-msse4.1`).

**Triangles in 3D** (`RT64_NABOO_3D_TRIANGLES=1`). Drawing the RDP's
triangles gives RT64 only screen positions, so no widescreen and no frame
interpolation. With this set, the interpreter stops at:

- the vertex transform's loads (`0x1890`, `0x195C`; `$13` the model space
  vertices, `$9` where they go, `$10` the count) to load the same vertices
  into RT64, with the combined matrix (DMEM `0x5C0`) and viewport (`0x130`);
- the triangle routine's start (`0x11A8`; `$11`-`$13` its vertices), to
  check they're the ones loaded (their clip coordinates in DMEM against the
  matrix's; on every captured task all match, within 3e-5);
- `0x12B0`, where the routine is about to write the RDP triangle, to draw
  it in RT64 and skip to the routine's return (`0x1744`).

Drawing at `0x12B0` rather than at the start matters: in between, the
microcode rejects triangles off screen, culls back faces, and (through the
`jalr` at `0x12A8`) sends the RDP the triangle's texture loads and combiner,
so a triangle drawn at the start used the previous one's texture. At
`0x12B0` the hook count equals the RDP triangle count on every captured task,
with nothing in between. A triangle that needs clipping reaches it once per
visible piece (through the clipping overlay); RT64 clips for itself, so the
whole triangle is drawn the first time. The RDP commands still waiting in
DMEM (`0xE80` up to `$16`) are sent to RT64 before each triangle.

Two more traps: RT64 takes the viewport with each vertex when it's loaded,
so it has to be set before the transform's first vertices, not at the first
triangle (or the first batch, usually the terrain, comes out stretched into
wedges); and a by-hand back-face test in clip space had its sign flipped by
the viewport's Y scale, which is why the microcode is now left to cull.
Checked side by side with the RDP triangles through the intro: same frames.

It stays off by default for now: it turns on RT64's frame interpolation,
which pairs each object's combined matrix with the next frame's and pulls
models apart (the detached limbs from round 16). Telling the camera apart
from the combined matrices is the next step, for interpolation and for
widescreen alike.

**Still black after the Factor 5 logo**, with the real microcode too. It's
not the rendering: what comes next is the title screen, a picture drawn as
548 texture rectangles from strips of RAM at `0x80436BC0`-`0x8044EBC0`, and
those strips are mostly zeros in the port. Something on the CPU side doesn't
fill them in; next to compare against mupen64plus's RAM.

### Before: reading the microcode for the terrain

**The microcode's layout** (`tools/rsp_disasm.py` disassembles it):

- 0x2950 bytes at `0x4003A8C0`; the first 0x1000 are resident at IMEM
  `0x1000`, the rest are overlays DMA'd in on demand.
- The command loop is at IMEM `0x1038`. Commands `C0`-`FF` go to the RDP
  as they are; the others jump through a table of IMEM addresses in DMEM
  (`06` call `0x1754`, `07` branch `0x176C`, `B8` return `0x1778`, `B4`/`BF`
  triangles `0x1B44`/`0x1B24`, `BE` `0x1B08`, `05` `0x1A60`).
- `05` loads the overlay whose id is its sub-mode (`w1 & 0x3F`; the ids
  are multiples of 3) through the loader at `0x1164`. Overlay table at DMEM
  `0x6A`, 3 bytes per id: IMEM entry (one of `0x1D60`/`0x1AA0`/`0x1CF0`,
  from the halfwords at DMEM `0x64`), length, and offset in the microcode.
  Terrain generation is `09` (0x29C bytes from +`0x1110`), `0C` (0x2C8 from
  +`0x13B0`) and `0F` (0x27C from +`0x1680`), all at `0x1CF0`; `15`, `18`,
  `24`, `27` load at `0x1D60`; `2A` is the environment-mapped texture
  coordinates, `12` (at `0x1AA0`) is likely the terrain polygons.

**The reference: the real microcode on captured tasks.** First tried
recompiling it with RSPRecomp's overlay slots, but its overlays don't fit
that model: the terrain overlays (at `0x1CF0`) overwrite code that the
`0x1D60` ones leave in place (the end of MOVEMEM's stream DMA and command
`02` live at `0x1CF0`-`0x1D5F`), so what's in IMEM depends on the history,
not on a fixed set of slots. Instead, `tools/rsp_reference.c` runs a task
captured with `NABOO_DUMP_GFX` on mupen64plus's low-level RSP plugin (a
real RSP interpreter, loaded at run time; a development tool, not part of
the game), with a stand-in RDP that finishes instantly, and saves the RDP
command stream. Notes: `InitiateRSP` clears DMEM and IMEM, so the task is
loaded after it; the RDP output is a ring buffer in RAM (`output_buff`) the
microcode hands over through `DPC_START`/`DPC_END`, waiting on
`DPC_STATUS & 0x400` and `DPC_CURRENT` when it wraps.
`tools/rdp_triangles.py` decodes the triangles and draws them flat: tasks
900 and 1200 of the intro show the snowy canyon under the STAPs, the
scenery the port doesn't draw yet (126, 573 and 897 triangles for tasks
600, 900, 1200).

**Next.** Implement `05` (`09`, `0C`, `0F`, `15`) and `BE` in the module
from the overlays' code, checking each against the reference triangles of
the same task.

## 2026-10-27, round 16: textures on the right faces

Tested on Windows after round 15: the LucasArts logo's textures were
misaligned (letters scattered across the L), and model limbs looked
detached; after the Factor 5 logo the screen went black.

**Textures.** A triangle command can run a short display list the game
attaches at DMEM `0x58C` (it loads the triangle's texture). The module ran
it after drawing the triangle, so every textured face used the previous
face's texture. GLideN64 adds the triangle, runs the list, and only draws
pending triangles afterwards (its tile and load commands don't flush), so
the list's texture is the triangle's own. Now the list runs before the
draw, and the LucasArts logo comes out right. (Texture coordinates were
checked along the way: s10.5 with perspective, as before; doubling them
made it worse.)

**Black after the Factor 5 logo.** The game carries on: those scenes are
built from the terrain and sky the microcode generates itself (`05`
sub-modes `09`/`0C`, `15`, and `BE`, 300-450 a frame), which the module
skips. That's the next piece. GLideN64's implementation of them is
GPL-2.0-only, which can't be combined with this GPL-3.0 project, so they
will be worked out from the microcode itself (readable through RSPRecomp's
output, and checkable by running the real microcode on the same DMEM).

## 2026-10-27, round 15: the intro right way up and running on

Tested on Windows after round 14: the Expansion Pak model showed, but at the
top of the screen instead of the bottom; the next screen (the STAP and the
N64 logo) was upside down, and froze partway through its animation.

**Upside down.** Factor 5's combined matrices already give screen Y the way
their microcode uses it, where standard microcode (and so RT64) flips it.
The module now negates the matrix's Y output after forcing it
(`flipMatrixY`, `lib-patches/rt64/0023`).

**The freeze.** The graphics thread never finished a display list: it
walked off the end of RAM (`0x00F19250`) through zeros. A guard that
logged the last commands showed how: command `05` with sub-mode `0F` is 64
bytes the first time and 8 bytes after that. The first one copies its data
into DMEM and sets the byte at `0x37E`, and `4F` clears it again. The
module read the flag but never set it, so the second `0F` skipped 64 bytes,
landed in data, and took `06030603 C0C0C0C0` for a call to `0xC0C0C0`. The
module now sets the flag (the data itself is for the terrain commands,
still to do).

Now the intro plays through: the Expansion Pak screen, the STAP shooting
the N64 logo apart, the craft flying past, and the Factor 5 logo fading
out. What follows is mostly dark: the terrain commands (`05`/`BE`).

## 2026-10-27, round 14: 3D models show; a Windows-only crash

Tested on Windows after round 13: the splash screen's 2D parts showed, the
rotating 3D Controller Pak didn't, then the game crashed with an uncaught
`ultramodern::thread_terminated`.

**No 3D.** RT64 draws with `world = MVP * inverse(projection) * projection`
when a combined matrix is forced, and the projection stack starts each task
as an all-zero matrix (`RSP::reset`). Battle for Naboo never loads a
projection of its own (its matrices are all combined), so every vertex came
out invalid on the GPU, while the CPU-side screen positions logged in round
13 looked right. The module now sets an identity projection at the start of
each display list (`lib-patches/rt64/0023`). The first 3D scene's models
now draw; its background and terrain (commands `05`/`BE`) don't yet.

**The crash.** The runtime ends a destroyed game thread by throwing
`thread_terminated` through the recompiled code and libultra's
`extern "C"` functions back to the thread's start. With CMake's default
`/EHsc`, clang-cl treats `extern "C"` functions as never throwing, so it can
drop that `catch`, and the exception ends the program (the log's
`func_4003EA30` is `osEepromLongRead`, the last game function on the stack;
the game creates and destroys loader threads around then). Linux compilers
don't make that assumption, which is why it didn't show here. The build now
uses `/EHs` (`CMakeLists.txt`).

## 2026-10-27, round 13: the graphics microcode in RT64; a symbol cut short

**Factor 5's microcode in RT64** (`lib-patches/rt64/0023`). A new GBI module,
`src/gbi/rt64_gbi_f3dnaboo.cpp`, picked by the microcode's hashes (text
`0x4B79F85075B03B40`, 0x1000 bytes from `0x4003A8C0`; data
`0x9AD45F4661015594`, 0xEC bytes from `0x4004E8E0`; computed the way
`getGBIForUCode` does, over RDRAM's word-swapped bytes). How the commands
behave follows olivieryuyu's decoding of this microcode for GLideN64
(`F5Indi_Naboo.cpp`, GPL-2.0: used as a description only, no code copied).
What it does:

- Display lists are 0x108-byte chunks (`[next, prev]` then 0x100 bytes of
  commands). A new `GBI::beginDisplayList` hook skips the task's first
  header; `06` (call) and `07` (branch) enter a chunk list, `B5` jumps to the
  next chunk, `B8` returns. The next-chunk pointer is kept per call level.
- The microcode keeps its state in DMEM, so the module keeps a DMEM model
  (in RDRAM's word-swapped layout), loaded with the task's 0xEC bytes of
  data and the words GLideN64 clears at the start of each task.
  `01` (MOVEMEM) DMAs from RDRAM, or from the stream that `02` sets up, then
  acts on the destination: viewport (`0x0101300F`), the combined matrix
  (`0x0105C03F`, through RT64's forced-matrix path), the texture-generation
  matrix (`0x010E403F`), vertices already in DMEM or picked from RDRAM by
  index, the light count, and lighting (ambient, directional and point
  lights into per-vertex colours at DMEM `0xD40`). New RSP entry points take
  vertices, a viewport and a matrix from data instead of an address.
- `B4`/`BF`: two or one triangles over vertices addressed as
  `(dmem - 0x600) / 40`, with each corner's colour from the lighting
  results and texture coordinates from the command (or generated from the
  vertex position, `w0 & 0x800`); 16 bytes, 32 when textured. Untextured
  ones aren't culled, as the microcode does. A short display list the game
  attaches at DMEM `0x58C` runs after the primitive.
- `BC` writes DMEM (fog multiplier and offset in 16.16, perspective
  normalisation), `BA`/`B9` set other-mode bits by mask (`B9` only when its
  flag matches DMEM `0x11C`), `B6` clears geometry mode with an inverted
  mask, `BB` also sets the texture-enable bit, `E4` is a 16-byte texture
  rectangle.
- Not drawn yet: `05` (terrain, particles and text generated by the
  microcode) and `BE` (polygons over that terrain); `05`'s parameters are
  skipped so the rest of the list stays in sync.

With it, the title screen's display lists run with 87 triangles and 337
text rectangles a frame, and the first 3D scene with about 130 triangles,
landing on screen at sensible positions (checked by logging).

**A crash about a minute in** (it happened before the graphics work too, and
on Windows would have been behind the black screen). The trail:

1. Thread 3 crashed on its own `OSThread`: its host context pointer had
   been overwritten. A per-frame list of 0x30-byte entries (filled by a
   recursive walk of the scene, `func_4000B6B0`) ran past its buffer, over
   the thread. The walk never ended because a node's `next` pointed at
   itself: `func_41008760` had inserted it into a list it already headed.
2. The camera structs the game renders with were wrong: zero, or full of
   another struct's floats. Run on mupen64plus (`tools/mupen_dump.c`), the
   same structs hold the screen sizes (e.g. `0x4004CB80` = 400x448).
   `func_4105AB40` copies them from `s0 + 8`, and `s0` was wrong after the
   call to `func_40039C74`, which returned with `sp` 0x18 too low and so
   restored its saved registers from the wrong place.
3. The cause: `func_4003A880` ended without its `jr $ra` delay slot
   (`addiu $sp, $sp, 0x18` at `0x4003A8B0`), so every call left the stack
   0x18 bytes low. The symbol generator's range for the RSP microcode
   started at `0x4003A8B0` instead of `0x4003A8C0` and cut the function to
   0x30 bytes. Fixed in `tools/splat_to_syms.py` and the symbol file (0x34).
   No other function ends on a branch without its delay slot (checked over
   all of `RecompiledFuncs/`).

With that, the viewport, the cameras and the screen sizes match the
emulator's frame for frame, and the game runs on without crashing.

Found along the way, all fixed:

- `func_400236E4` has no return and runs on into `func_40023734`;
  recompiled functions don't fall through, so a hook calls it
  (`naboo.us.rev0.toml`). It's the only one.
- The runtime's `thread_queue_remove` only ever looked at the head of the
  queue (`lib-patches/N64ModernRuntime/0005`): a thread destroyed further
  down stayed queued with no context. The game creates and destroys three
  loader threads at a time, so this would have bitten.
- `osStopThread` on another thread did nothing (an `assert`), and
  `osStartThread` would queue a thread that was already running or waiting:
  the game pauses one of its threads this way (`func_400031B4` /
  `func_40003210`). Now it's taken off its queue and marked stopped, and
  only a stopped thread can be started (`0006`). `osSetThreadPri` re-queued
  through the cleared queue pointer; fixed in the same patch.
- The game calls `osViBlack(TRUE)` at boot and never turns it off: on the
  N64, `osViSetMode` replaces the VI state and clears it. The runtime's
  didn't, so the picture stayed black whatever was drawn (`0007`).
- `NABOO_NO_AUDIO=1` skips the audio tasks, and
  `-DNABOO_FUNCS_DEBUG_FLAGS=-fno-inline` builds the recompiled code so a
  debugger names the game function it's in (both used to rule things out
  here).

**Next.** The window now shows white. The game sets a custom interlaced mode
(`0x40080180`: 16-bit, 400 wide, x scale `0x280`, two fields with origins
`0x320`/`0x640`, y scale 2.0), and the framebuffers are `0x805F6000` and
`0x8061B800`. Find out what RT64 makes of that mode, then the `05`/`BE`
terrain commands.

## 2026-10-27, round 12: a frame-wait busy loop; first 3D frames captured

After about 30 graphics tasks thread 3 (game logic, now in `.paged`:
`func_41000000` -> ... -> `func_40024530`) sat at full CPU in
`func_40037A60`: it stores a frame count at `0x40090512` and spins until the
VI side counts it down to 0, with no OS call in the loop. On the N64 the VI
interrupt preempts it; here game threads only switch at OS calls. A hook at
the loop head (`0x40037A7C`) calls `yield_self_1ms`, which delivers pending
events and lets the higher-priority thread run. The BattleTanx port fixed
its busy-waits the same way.

With that the game runs on: graphics tasks 100 and 250 captured
(`NABOO_DUMP_GFX`). Display lists decoded with `tools/naboo_dl.py`:

- Same chunk layout as Rogue Squadron's: 0x108-byte chunks from a pool,
  header `[next, previous]` then 0x100 bytes of commands; `06` calls a list
  by its header address, `B8` returns. Static lists in `.main`'s data (e.g.
  `0x4004CC20`) have the same header.
- Task 30 and 100 (boot): standard RDP setup only: SETCIMG (320 wide at
  `0x805F6000`), SETZIMG (`0x8071B800`), FILLRECT clears, SETOTHERMODE,
  MOVEWORD, prim/fog colours, ED (SETSCISSOR), and `01` (Factor 5 MOVEMEM,
  a direct copy into DMEM).
- Task 250: 3D. `B4` (Factor 5 TRI2) 27 times, `BF` (TRI1), `B5` (jump
  list), `02` (set list address), TEXTURE, SETCOMBINE, SETENVCOLOR. The
  walker still reads the extra words of the multi-word commands (the
  textured triangle forms are 32 bytes) as commands of their own: the next
  step is each command's real length, from the microcode and GLideN64's
  description of it.

The plan from here (owner's choice, for proper widescreen and
interpolation): a Battle for Naboo GBI in RT64 (a lib-patch adding a module
like RT64's F3DEX ones), detected by the microcode's hash, with a model of
the microcode's DMEM rebuilt from the task's ucode data at each task (the
microcode keeps its display-list streaming state and tables there, as
GLideN64's implementation shows). References: GLideN64's
`F5Indi_Naboo.cpp` (GPL-2.0 only: for what each command does, no code),
the Rogue Squadron RT64 fork's `rt64_gbi_f3dfactor5.cpp` (MIT, for an
earlier version of the microcode), and n64js (MIT), which runs this
microcode on its RSP.

## 2026-10-27, round 11: round 9's fix was racy; a frame capture tool

**Report (owner, Windows):** still a black screen (expected), but testing
here showed the game back at round 7's file-loader stall in most runs.

Traced with a hardware watchpoint on `__osRunningThread`: right after it
starts, thread 3 (the requester) destroys the boot thread (`func_40039960`).
The runtime destroys another thread by waking it so it can clean itself up,
and round 9's write ran on that wake-up, before the check that ends the
thread: the dying boot thread set `__osRunningThread` to itself while thread
3 was running, and thread 3's request then went out with no sender. Round 10
only got through because the timing went the other way. The write now comes
after the check (`lib-patches/N64ModernRuntime/0004`). Three runs in a row
reach the graphics tasks.

**Frame capture** (`src/game/gfx_dump.cpp`): `NABOO_DUMP_GFX=5,30` (task
numbers) saves the N64's RAM, big-endian, and the task at those graphics
tasks, to `NABOO_DUMP_DIR`. Hooked at the start of the game's RSP task
launcher, `func_40023558`. First captures, task at `0x4004D9B0`:

| OSTask field | value |
|---|---|
| type, flags | 1 (M_GFXTASK), 0 |
| ucode_boot, size | `0x4003A8C0`, 0x1000 (the microcode itself) |
| ucode, size | `0x4003A8C0`, 0x1000 |
| ucode_data, size | `0x4004E8E0`, 0xEC |
| dram_stack, size | 0, 0x400 |
| output_buff, end | `0x8068B000`, `0x806AAFF0` (RDP commands go to RAM, 128 KB) |
| data_ptr (display list) | `0x80659318` (task 5), `0x80659108` (task 30) |
| yield_data_ptr | `0x800CA8B0` |

## 2026-10-26, round 10: the first graphics task, and GLideN64

With round 9, the game gets through its file loading and on to its first
graphics task (and two audio buffers). RT64 then reports "Unable to find a
matching GBI in the current database. This game is not supported in HLE."
and crashed in `Interpreter::processDisplayLists`, which assumes a GBI was
found. `lib-patches/rt64/0022` skips the display lists instead, so the game
keeps running with a black screen until the microcode is supported.

Verified here (90 s under gdb, Xvfb and software Vulkan, so slow): no crash,
and the game runs its main loop: 137 RSP tasks, 47 osViSwapBuffer, 93
controller reads, 5,320 audio buffers (about one per VI), 9 ROM reads.

**References for the microcode** (owner's pointers): GLideN64 implemented
Factor 5's microcode for Indiana Jones and Battle for Naboo in 2018
(github.com/gonetz/GLideN64 issue #1259, commit b0a5753, reverse engineered
by olivieryuyu). The issue's microcode comparison found a section common to
all three Factor 5 games and another only in Battle for Naboo and Indiana
Jones, which agrees with round 5 (Naboo's dispatch tables are not where Rogue
Squadron's are). GLideN64 is GPL-2.0 only, which can't be combined with this
project's GPL-3.0 (or put into RT64, MIT), so its code can't be copied: it is
a reference for what the commands do, for an implementation of our own.

## 2026-10-26, round 9: the file loader (__osRunningThread)

Round 7's stall, read from game memory (gdb, with the crash handler's RDRAM
pointer, which this port never set before: `naboo_on_init` now does, so
crash logs can also name N64 addresses).

- The loader (`func_40004308`) had one request in slot 0 and was stuck
  replying to it: `func_4000664C` retries a non-blocking osSendMesg until it
  succeeds, and the reply went to a queue at address `0x2C`. The request's
  sender field was 0.
- The requester (`func_40004850`) fills that field from `func_40006780`,
  which finds the current "process" by walking the game's process list
  (`0x4007D9A4`, next at +0x1C) for the one whose thread (+0x48) is
  `__osRunningThread` (libultra's variable at `0x4004ECD0`). The runtime
  switches threads itself and never wrote that variable, so it was 0.
- The game reads `__osRunningThread` in about 40 of its own functions.

**Fix** (`lib-patches/N64ModernRuntime/0004`): the runtime can be given the
address of the game's `__osRunningThread`
(`ultramodern::set_running_thread_address`) and writes the running thread
there whenever a game thread resumes. `naboo_on_init` sets `0x4004ECD0`.

Also, `osYieldThread` (round 6) now does what libultra's does: switch to the
next ready thread of the same or higher priority (the runtime's scheduler
only switches to a higher one), and only wait for an event when there is
none. This wasn't the cause of the stall, but the old version never let a
thread of the same priority run.

## 2026-10-26, round 8: the window wouldn't close

**Report (owner, Windows):** black screen, and the window couldn't be closed.

In game, the frontend answers a close with its quit prompt, which is drawn
and run by the per-frame UI pump. The BattleTanx port calls that pump from
the game's VI-swap function (a patch in `patches/`, its round 61); this port
has no such patch yet, and the game is stalled anyway (round 7), so the
prompt never showed and the close did nothing. A confirmed quit also waits
for the game's threads, which can be stuck.

**Fix** (`src/main/main.cpp`, `close_watch`): a second close request exits at
once, and once a quit has begun the process exits if it hasn't finished
within 3 seconds. Verified here: the first close (SIGINT, which SDL turns
into the same quit event) leaves the game running, the second ends it.

## 2026-10-26, round 7: VI events, and the file-loading thread

**VI.** With the idle loop fixed (round 6), the boot thread's init
(`func_40039960` -> `func_40017928` -> `func_40021968` -> `func_400231C8`)
waited forever on queue `0x400800E0`, which the VI thread (`func_40022960`,
registered with osViSetEvent) fills on a retrace. No VI message was ever
sent: the runtime's VI thread takes the game's event queue only when it
switches to a new VI mode, and the game sets its mode (osViSetMode) only
after that first retrace. Normally the launcher's placeholder mode hides
this; starting with `--game` (no launcher) the game got there first. The
hardware raises VI interrupts whatever the mode, so
`lib-patches/N64ModernRuntime/0003` hands the queue over even when no mode
is set yet. After it: osViSetMode, osViSwapBuffer and three ROM reads.

**Next stall.** Then a thread running `func_40004308` takes all of the CPU:
the game's file loader. It blocks for its first request
(`func_40006618`, osRecvMesg on its queue at +0x2C), then polls for more
without blocking (`func_400065D8`) while it steps through up to 8 transfer
slots (`0x4007BE30`, 0x74 bytes each; `func_40003C88`, `func_40003D28`,
`func_40001AFC`). The thread that sent it the request, `func_40004850`
(called from `func_40001640` <- `func_4003A324`), was preempted inside
osSendMesg and never runs again, as it is lower priority. Not solved yet:
which slot state it is waiting in, and what is meant to move it on (a PI
completion, or that lower-priority thread).

## 2026-10-26, round 6: first boot

Built on Linux (clang, Ninja) and run under Xvfb with Mesa's software Vulkan
(lavapipe), starting the game directly with `--game naboo`.

- `Failed to find function at 0x4003A548`, then `0x40022C78`: thread entry
  points passed to osCreateThread, which the split had merged into the
  function before them (the first sits after `main()`'s `jr $ra`, the second
  after an endless `j` loop). `tools/splat_to_syms.py` now also starts a
  function at every code address built with `lui`/`addiu`, and accepts a
  `j` and its delay slot as the end of the previous function for code
  pointers in data. 2,333 functions.
- Link errors for libultra internals the game calls itself, not through the
  replaced library functions:
  - `func_40023558` starts an RSP task by hand: `osSpTaskLoad`, a wait on
    `__osSpDeviceBusy`, a DMA of the ucode data to DMEM with
    `__osSpRawStartDma`, then `osSpTaskStartGo`. The runtime's task start
    copies the ucode data itself, so both are no-ops that succeed.
  - `osDpGetCounters` (`func_4000B37C`, performance counters): zeros.
  - `osMapTLB` (the memory setup, below): nothing to do.
  - `osViGetCurrentField`: field 0.
  All in `src/game/stock_runtime_compat.cpp`.
- The memory setup, `func_40039960`, decides between two ways of running
  `.paged`: paging it in (it installs the TLB-miss handler `func_400008F4`
  and sets up the page pool, `func_4003A3E0`), or mapping the whole segment
  onto the top of RAM with `osMapTLB` in 128 KB steps and DMAing it there
  from ROM `0x6F370`. Either way the segment is only read, and the port's
  copy at `0x01000000` (round 4) is what `0x41xxxxxx` reaches, so
  `osMapTLB` has nothing to do.
- After that, every game thread sat in `osRecvMesg` except the boot thread
  `func_4003A548`, which spins: it starts the VI manager, creates the two
  main threads (`func_4003A5EC` priority 127, `func_40039960` priority 9),
  then loops on `osYieldThread()` at priority 0. The runtime's
  `osYieldThread_recomp` is `assert(false)`, which is nothing in a Release
  build, so the loop never gave anything a turn. The game calls
  `osYieldThread` in 13 places.
  **Fix** (`lib-patches/N64ModernRuntime/0002`): `osYieldThread` delivers
  pending events (waiting up to 1 ms for one, so the idle loop doesn't burn
  a core) and briefly drops the thread to priority 0, so a ready thread of
  the same priority runs. This is the BattleTanx port's busy-wait fix
  (its rounds 50-52 and 65) built into the call the game already makes.

## 2026-10-26, round 5: osInitialize, audio microcode, saves

**osInitialize.** `main()` calls `func_4003A108`, not libultra's
`osInitialize` at `0x400408C0` (never called): Factor 5's own copy, which no
signature matches. Named `osInitialize` in `syms/libultra.txt` so the
runtime's replaces it, and libultra's unused copy is stubbed. Like the
BattleTanx port's (its round 138), the game's version ends by scaling
`osClockRate` (u64 at `0x4004EC40`) by 3/4, from 62.5 MHz to the CPU count
rate 46.875 MHz, and sets `osViClock` (`0x4004EC48`) for the TV type. The
runtime's doesn't, which in the BattleTanx port made every timer run at
3/4 speed. A hook in `main()` right after the call (`0x4003A504`) does both
(`src/game/os_init.cpp`; NTSC: 48,681,812 Hz).

**Audio task.** Set up in `func_40035340`: type 2 (`M_AUDTASK`), boot
`0x40001080` (Factor 5's RSP boot, 0xD0 bytes), ucode `0x4003D210` size
0x1000, ucode data `0x4004E9D0` size 0x800. This is MusyX, as in Rogue
Squadron. `musyx.us.rev0.toml` runs RSPRecomp on it with the standard layout
(text at IMEM `0x1080`, 0xF80 bytes). Not finished: the code runs up to IMEM
`0x1FFC` and continues past the end of IMEM (the RSP's PC wraps to `0x1000`),
so the last 0x80 bytes of the 0x1000 text belong at IMEM 0-0x7F; and the
indirect jump targets aren't known yet (the table at DMEM 0x20 is a resample
filter, not a dispatch table). RSPRecomp also wrote invalid C (`0 = ...`) for
the data words in the text that decode as shifts into `$zero`:
`lib-patches/N64Recomp/0004` makes those nops.

**Graphics task.** Set up in `func_40023080`: ucode `0x4003A8C0` used as
both boot and ucode (0x1000 bytes), data `0x4004E8E0`. Its command dispatch
(`sra 23; andi 0x1FE; lhu table`, IMEM `0x10D0`) has the same shape as the
one the Rogue Squadron recomp documents for Factor 5's microcode
(docs/factor5-ucode-dispatch.md there), but its tables are at other DMEM
offsets (`0xB6` and `-0x9C` from the index, against Rogue Squadron's `0xD6`
and `0x64`): a later build of the microcode, so the Rogue Squadron RT64 fork
can't be assumed to fit as is. Stock RT64 doesn't handle either.

**Saves.** The game links the EEPROM functions and only checks that one is
present (`func_40002D80`, `func_400055F0`), so `SaveType::AllowAll` (a
16 Kbit EEPROM) as in the BattleTanx port. Also links the Controller Pak and
Rumble Pak code; the General tab's accessory option defaults to the Rumble
Pak.

## 2026-10-26, round 4: recompiling

`tools/splat_to_syms.py` turns the split into the symbol file and fixes it;
each fix was found by an N64Recomp error:

- The entrypoint must be at ROM 0x1000: the boot stub (`func_40000400`,
  renamed `recomp_entrypoint`). Its three COP0 writes and the `jr $t0` to the
  TLB address are nopped, so it falls through to its own `j main`. Its stack
  (`0x803FFFF0`) is fine as is.
- `main()` (`0x4003A4DC`) had been merged into the function before it: added
  as a start by hand.
- Function starts at every `jal` target and at code pointers in data.
- Jump tables: N64Recomp reads a jump table from the function's own section,
  but `.paged`'s jump tables are in `.main`'s data (`0x4005xxxx`), which
  crashed it. `lib-patches/N64Recomp/0003` looks up the section that holds
  the table. The sections in the symbol file now include their data.
  splat also starts a function at every case of a `switch` after a
  `jr $ra`; the generator finds jump tables from the code (`lui`/`addiu`,
  `lw`, `jr`, sized by the `sltiu` before them) and merges those back.
- Data in the code: functions that run into words that aren't instructions
  (a `1.0f`, strings, tables) end before them; pieces that are only data
  are dropped. `0x4004E880` is a small function in `.data` (allocates data
  cache lines over a buffer, stubbed).
- RSP microcode in `.main`'s code: Factor 5's RSP boot `0x40001080`-
  `0x40001150` and the graphics/audio microcode `0x4003A8B0`-`0x4003E630`.
- The hand-written routines at the end of `.paged` (`0x410D9C60`-
  `0x410DA330`) read tables placed after their code through the return
  address of a `bltzal $zero` (never taken, but it links). Each of the six
  becomes a `lui`/`addiu` of the same address; the tables are left out of
  the functions.
- `uses_mips3_float_mode = true`: Factor 5's vector math (e.g.
  `func_41002400`) sets the FR bit in Status to use all 32 FPU registers as
  doubles, then clears it. N64Recomp follows Status writes in that mode.
- `cache` instructions in game code are nopped (bzero `func_40000F50` and
  others); the TLB setup functions and the TLB-miss handler are stubbed.
- `lib-patches/N64Recomp/0002`: integer division by zero. On a PC it's a
  crash; on the N64 it gives lo = -1 (1 for a negative dividend) and
  hi = the dividend. N64Recomp now emits that for `div`/`divu` (the
  BattleTanx port needed a hook per division for the same thing).

Result: 2,308 functions (2,333 after round 6), no errors or warnings.

## 2026-10-26, round 3: symbols

splat 0.50 / spimdisasm 1.42 on both segments (`tools/splat.yaml`): 582
functions in `.main`, 1,597 in `.paged`. libultra was named with n64sym's
signature file, matched by a small Python reimplementation of its matching
(`tools/match_signatures.py`: CRC32 of each function with its relocated
fields zeroed), iterated with splitting until nothing new matched: 169 names
in `0x4003E630`-`0x4004B6D0`, `syms/libultra.txt`. Matches with more than one
name were settled by libultra's object order: `0x400471E0` is
`__osPiCreateAccessQueue` (with `__osPiGetAccess`/`__osPiRelAccess`, not the
Si ones), `0x40041A90` `__ll_rem`, `0x40041F40` `osMotorStart` (after
`osMotorStop`, as in that libultra's `motor.c`), `0x40045C10`
`__osPfsGetInitData`. Tiny matches elsewhere (8-28 bytes, e.g. `__dummy`)
were false and are ignored. `0x40046200` has the same bytes as `__osGetId`
and is left unnamed.

## 2026-10-26, round 2: the paged segment

The TLB-miss handler (`0x400009CC`, installed at the exception vector by
`func_400008F4`) serves misses in `0x41000000`-up: it takes a 4 KB page from
a pool, maps it with `tlbwi`, and DMAs it from the cartridge at
`[0x80000474] + (vaddr - 0x41000000)` with the PI registers. So `.paged` is
code (and some data) that only ever exists in RAM a page at a time.
Confirmed where it comes from with the `jal` targets into `0x41xxxxxx`:
with ROM `0x6F370` as the base, 863 of them land on a function prologue; with
the base 0x370 lower or higher, under 20. The segment's code ends at ROM
`0x1496A0` (`0x410DA330`), followed by 0x60 bytes of data, then the asset
file table (`dbg_data` at `0x1496E0`) and zlib data.

On the PC the code is recompiled ahead of time, so only reads of the
segment's data need it in memory. Every game address is reduced to its
physical offset, `addr & 0x1FFFFFFF` (`lib-patches/N64Recomp/0001`: the
`MEM_*` macros in `recomp.h`; `lib-patches/N64ModernRuntime/0001`:
`TO_PTR` and `osVirtualToPhysical`). That leaves KSEG0 and KSEG1 as they
were, sends `0x40xxxxxx` to the same RDRAM as `0x80xxxxxx` (what the TLB
does), and `0x41xxxxxx` to `0x01xxxxxx`, where `src/game/paged_segment.cpp`
loads the segment from ROM at boot. The runtime's mod memory, which started
at `0x81000000`, moves to `0x82000000` to make room. The runtime also
registered `.paged` at the wrong address (as if IPL3 had loaded it with
the first MB); the same file moves it to `0x41000000`.

## 2026-10-26, round 1: the ROM

The dump is byte-swapped (`.v64` data in a `.n64` file); normalized with
`tools/normalize_rom.py`. Header: "Battle for Naboo", `NNAE`, entry
`0x80000400`, 32 MB. Details in `syms/rom_info.md`.

The boot stub at ROM `0x1000` sets the stack to `0x803FFFF0`, clears the TLB
(`func_80000880`), maps virtual `0x40000000` onto physical 0 with two 4 MB
pages (`func_80000F08`: PageMask `0x7FE000`), and jumps to `0x4000044C`
through `$t0`, which goes on to `main()` at `0x4003A4DC`. Everything after
that runs at `0x40xxxxxx`: the game is linked for the TLB window, not
KSEG0. `main()` clears BSS (`0x4006E770`, 0x38D70 bytes) and calls
osInitialize, then starts the boot thread.

A byte scan shows code in ROM `0x1000`-`0x4C2D0` and `0x6F370`-`0x1496A0`,
and RSP microcode inside the first (`0x3B400`-`0x3F200`). The
`jal` targets in the second range point at `0x41xxxxxx`: a second segment
linked at `0x41000000` (round 2).

References used: the Rogue Squadron recomp
(github.com/MikeSemicolonD/RogueSquadron64Recomp) and decomp
(github.com/Tmcg2/rogue_squadron64), for Factor 5's microcode and engine.
The Rogue Squadron recomp has nothing on the TLB (its code runs from KSEG0
with overlays), so the memory layout here is new work.
