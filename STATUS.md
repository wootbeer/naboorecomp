# Status

The project's working log: every investigation and fix, newest first, as
"rounds". PROGRESS.md has the short version of where things stand; this file
is for finding out why something is the way it is. Same format as the
BattleTanx: Global Assault port's STATUS.md, whose lessons are referred to as
"the BattleTanx port, round N".

## Overview (as of round 26)

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

## 2026-11-14, round 26: the heap corruption found; audio headroom and a limiter; loads 6x faster; no thread spinning

**The heap corruption, found** (rounds 22-24's garbage cell rectangle,
freezes and crashes in open levels). The heap checker caught it here, 8.5
minutes into a soak of this round's build:

    [NABOO HEAP] heap 0 broken, found at the terrain's end (func_4104E8CC)
    (last good check: the terrain's start): bad size, block 80223900
    block before it: 80223410, size 4E0, flags 02

The block before it is the terrain's row tables, its last words
`00240001 08000000 10000000 10000000`, and the broken header's size was
`10000000` too. `func_4104EA9C` allocates 0x4E0 bytes for four tables of
each grid row's first and last column (two shorts a row): two of 100 rows
for the fine level of detail (pointers at `0x40099128`, `0x4009912C`) and
two of 56 for the coarse one (`0x40099130`, `0x40099134`, at +0x320 and
+0x400). After filling the rows a footprint covers, the game marks the
three rows past the last one as empty (first 0x1000, last 0: `func_4104E374`
at `0x4104E7EC` for the fine level). A footprint whose last coarse row is 53
puts the third mark in entry 56 of the last table, which is the next heap
block's size. The 4:3 footprint reaches about row 47 (round 23's numbers:
47 without widening, 52 with, in a short run), so the original never gets
there; the widescreen one does, now and then. Round 23 had the right suspect
and missed it: the tables were traced by 256-byte block, and one entry over
the end is in the same block as the table's own last entries.

Fix: the block is 0x1000 bytes and each table 256 rows, by patching the
four instructions in `func_4104EA9C` that size and place them (0x4E0,
+0x190, +0x320, +0x400 to 0x1000, +0x400, +0x800, +0xC00); the tables are
only reached through their pointers.

Checked by forcing it: `NABOO_TERRAIN_TEST_SCALE=1.6` widens the footprint
that much more, and with `NABOO_HEAP_CHECK=2` (every allocation) the coarse
level's last row reached 66 and the fine level's 117, both past the old
tables, through 60,000 clean checks. A plain 13-minute soak (170,000 checks)
was clean too, though its coarse level only got to row 52. The fine level's
117 is past the 100 rows the game itself expects (it caps one of its own
row counts at 100), so the terrain's widening now stops at 16:9
(`naboo_widen_terrain`): at 16:9 the fine level reaches row 89. On a wider
window the terrain ends where a 16:9 window's sides would be.

**The crackle at loud moments.** The game's own mix passes full scale: with
room to spare, the crawl's theme peaks at 49,872 against 32,767 (1.5x). The
microcode adds its voices into 16-bit buffers with saturating adds, so
those peaks came out flat-topped (465 clipped frames in the crawl), which is
the crackle; an emulator does the same (round 21). Now:

- The mix is made at a quarter of its level. Each voice's volumes are a
  ramp in the audio task's data: four 16.16 start values and four per-sample
  steps at the start of each 0x50-byte voice, 32 voices to a 0xA28-byte
  frame block after its 0x28-byte header (the task's data size is the
  number of blocks, 3 here). The microcode reads the voices with DMAs, and
  a new callback in the RSP interpreter (`dmaRead`, `lib-patches/rt64/0027`)
  shifts the ramps right by 2 in the copy it gets (`src/game/rsp_audio.cpp`,
  `NABOO_AUDIO_HEADROOM=0..3`). Everything after the ramps is linear, so the
  output is the same mix 12 dB down, with nothing saturating (the crawl's
  peak: 12,468, and 24,836 with a shift of 1). Changing the ramps in RAM
  for the length of the task was tried first and dropped: the game writes
  the next frame's voices while a task runs.
- `queue_samples` (`src/main/main.cpp`) makes up the gain in floating point
  and limits: the sound is delayed 48 frames (2 ms), the gain follows the
  loudest frame in that window down (so it's there before the peak goes
  out) and recovers over 150 ms. The crawl now peaks at 0.97 of full scale
  with no clipped samples; its loudest second is 1.5 dB quieter than before
  (it was clipping), the rest unchanged. `NABOO_AUDIO_LIMITED_OUT=file`
  saves what goes to the sound card.

Still there, and older than this round: about 30 of the crawl run's 12,000
buffers are copies of an earlier one (round 20's stale buffers), all at
screen changes.

**Loads.** `NABOO_PERF=1` now also reports every wait of 250 ms or more
between two graphics tasks ("the game sent no graphics task for N ms").
From boot into a mission those added up to 7.7 s, 3.0 s of it loading the
level. The time went into sleeps: only one game thread runs at a time, and
`yield_self_1ms` (behind `osYieldThread` when no other thread is ready)
slept up to 1 ms on every call, with every other game thread stopped. The
file loader yields constantly: its I/O thread after each chunk
(`func_40005170`), its manager thread (`func_40004308`) on every pass of a
loop that runs while any request is pending, the sound engine after each
batch. By call site, 3,800 sleeps each for the first two on the way into a
mission.

`yield_self_1ms` now sleeps only when nothing has happened since the
calling thread's previous yield (`lib-patches/N64ModernRuntime/0012`): a
counter, `ultramodern_activity`, counts events delivered from outside the
game threads and ROM reads started, and the manager loop's hook adds a
change in its eight request slots (`src/game/loader_progress.cpp`; the
runtime can't see that loop's own progress). Switches between game threads
don't count: counting them let two polling threads keep each other awake.
Result: 7.7 s of waits down to 1.3 s; the level load 3.0 s to about 0.35 s
(`NABOO_YIELD_ALWAYS_SLEEP=1` gives the old behaviour to compare).

Tried on the way: skipping the sleep whenever the thread had sent or
received a message or 100 us had passed (loads as fast, but a thread took
89% of a core at the title: the game's mutexes are message queues, so every
poll "did something"), and skipping it only after a ROM read (half the
gain: most of the manager's passes aren't next to a read).

**Two loops that spin.** Round 25 found no thread near 100% and called the
busiest one's 24-36% work. It wasn't: with the sleeps gone it went to
80-89%, and gdb showed two loops with no wait in them, which the loader's
sleeps had been slowing down by accident:

- `func_4108E8A0` (the title's and menus' scene loop) polls its message
  queue without waiting (`func_400065D8`) and goes straight back when
  there's nothing to draw. Hook at `0x4108EA04`: yield when `$s4` is clear.
- `func_41009EDC`, a priority-4 worker thread that brings in the scene's
  pending items when there's memory to spare: lock, walk the list, unlock,
  again. Hooks: a pass that brought nothing in yields before the next.

**Where a slow frame goes.** `NABOO_PERF=1` now reports each end of frame of
30 ms or more with the parts of `State::fullSync` that took over 1 ms
("slow end of frame, 120 ms: decoding textures 3, waiting for the render
thread 110"), from marks RT64 leaves through `rt64_perf_mark`
(`lib-patches/rt64/0027`): checking the tiles, starting the uploads, the
ubershader's pipelines, rendering to RAM, decoding textures, uploading draw
data, uploading textures, preparing the workload, waiting for the render
thread. For the user's 100-200 ms spikes, which don't happen here.

Per-thread CPU now (Xvfb, software rendering): title and Select Game 1.2-1.5%
for the busiest game thread (24-36% before), in play 2% (57% before, 78%
without the hooks). Checked: boot, menus, the crawl, a mission; a 10-minute
soak in play with `NABOO_HEAP_CHECK=1`.

## 2026-11-10, round 25: no game thread burns a core while waiting

Checked after BattleTanx GA's round 188 (its main thread used ~93% of a
core at all times, through a yield hook that returned at once and was called
back to back). Here, under Xvfb with software rendering, per thread
(`top -H`) and with gdb backtrace samples of the busiest game thread:

- LucasArts logo: busiest game thread 33%, the process manager
  (`func_41009EDC`) handing off between the game's processes with
  `osSendMesg`/`osRecvMesg`.
- Title (a live 3D attract demo of Theed): the scene's process thread 24-36%;
  in 15 of 20 samples it was asleep on a futex (blocked in a message wait).
- Select Game (a 3D preview plays in the circle): 31%; 19 of 25 samples
  blocked in `osRecvMesg` from `func_400065D8` (a non-blocking receive, which
  in the runtime hands the CPU to ready threads and sleeps), the rest in the
  scene's own code (`func_4108E8A0`).
- Every other game thread: 0-1%. Most of the process's CPU here is llvmpipe
  rendering in software.

So no thread spins: the busy ones sleep most of the time and work the rest.
The busy-wait hooks this project added (round 12 `func_40037A60`, round 18
`func_40033584`) and its `osYieldThread` (lib-patches/N64ModernRuntime/0002)
call `yield_self_1ms`, which sleeps until the next external message (at most
1 ms) on every call, so none returns at once the way BattleTanx's did. Nothing
changed.

## 2026-11-04, round 24: a heap checker

The user's next test (round 23's build): the terrain reached both sides, then
a freeze (the thread dump lists OSThreads at `0x80267238` and `0x802D03E8`
full of garbage) and a crash in `func_4000193C` reading `0xA0000000`.
`func_4000193C` is the game's allocator (`func_40001AFC` frees): it walks
the heap's blocks, 16-byte headers (+0 the data's size, +8 flags: 1 free,
2 used, 8 the end; +9 a tag), each followed by its data, so the next header
is at header + 0x10 + size. A header had been overwritten. Round 22's cell
rectangle at `0xA0000000` fits the same picture.

`src/game/heap_check.cpp` walks the heaps (from the pointers at `0x4007B178`,
`0x4007B164` and `0x4007B160`) with `NABOO_HEAP_CHECK=1`: once a frame (the
projection hook) and at the terrain builder's entry and exit; `=2` adds
every allocation (a hook at `func_4000193C`). The first broken header is
reported once, with where it was found, the last check that passed, and
the block before it (header and last 0x80 bytes, usually the one that ran
over, its tag naming what it was allocated for). Here the first heap holds
about 300 blocks; 20 minutes of checks found nothing.

## 2026-11-03, round 23: the terrain's widening, made safe

Round 22 turned the terrain's widening off (`NABOO_TERRAIN_WIDEN=1` to test
it), as level 2 corrupted the game's memory within minutes with it on. The
user's freeze log shows the run queue holding OSThreads that live in the
game's heap (`0x800CBB88`, next `0x8010C1F8`): the game's processes keep
their OSThread in a heap block. The crash log's stack ran from the terrain
(`func_4104E8CC` > `func_4104A708` > the display list chunk allocator,
`func_40006BC8`) into the scheduler, which read a garbage thread.

**What the terrain code writes doesn't change.** Every store in
`func_41049864`-`func_4105422C` (the terrain's file, `funcs_24.c`) was
traced to 256-byte blocks, per function, with widening on and off (a
temporary `sed` over the generated file). Both runs wrote the same buffers:
the display list chunks (`0x4F5400` up), the row tables (`0x217500`,
`0x223400`) and its globals at `0x40099100`. The wider footprint only uses
more of them. Its stack frames reach the same depth either way, the chunk
pool never ran low (~1200 of its chunks free, no evictions through
`func_4001ED74`), and the coarse level's cache rows run the same range.
20-minute soaks with it on didn't crash here, flying low or climbing.

**Who else reads the corners.** Every load and store in the game was
checked (temporarily, in `recomp.h`'s `MEM_W`) for the camera's eye and far
corners (`0x40060DBC` + `0x80` to `+0xBC`). Besides the terrain
(`func_4104A1CC`) and `func_41024064`, which sets them, four other systems
read all four corners: `func_410244B8` and `func_41027264` (the camera's
update), `func_4102CE20`, and `func_40023BB0` (called from objects). Round
22 widened the corners where they're made, so all of them saw the wider
view; with widening off they see the 4:3 one, the state the user's
24 minutes ran clean in.

**The change.** The corners are widened only while `func_4104E8CC` builds
the terrain, and put back as it returns (hooks at its entry and at its
`jr $ra`). They are a rectangle seen from the eye, 2.0 wide to 1.4 high in
camera space; the pairs of corners on its long sides are moved apart from
their midpoints by the widescreen scale, which widens its camera-space x
exactly, as round 22's change to the direction did. The long sides are
found by length (the pairing in between the short sides and the
diagonals), and anything that isn't a rectangle is left alone. The 4:3
corners are put back only if the corners are still the widened ones (the
terrain can wait on the chunk pool's lock while other threads run). Not in
cutscenes. On by default again; `NABOO_NO_TERRAIN_WIDEN=1` turns it off.
The hills reach both sides as in round 22 (checked side by side with it
off: the jagged edge at the right is gone).

If the corruption comes back with this, it's in the terrain code after
all; `NABOO_NO_TERRAIN_WIDEN=1` is the test.

## 2026-11-01, round 22: widescreen that widens, and a HUD that lines up

**The radar's two halves came apart in widescreen** (the ring one way, the
map another), and the side icons moved out. Measured against "Original":
everything drawn in 3D, HUD included, was 1.25x wider and further out, and
only the 2D rectangles (the radar's map) stayed put. The 16:9 picture of
round 20 was the 4:3 one stretched, not widened. Two of RT64's tests had
kept the framebuffer's square-pixel shape that round 20's
`aspectRatioSource` replaced with the TV's (`lib-patches/rt64/0026`):

- `FramebufferRenderer` widens a frame only if its scissor is close to the
  source shape. The scissor is in framebuffer pixels (400x444 here), so it
  never matched 1.43, and nothing was widened: no projection, no viewport,
  no rectangle. It's now compared with the framebuffer's own shape
  (`aspectRatioFramebuffer`, from the VI as before).
- A 3D view is widened (its projection, not just its viewport) only if
  its viewport is wider than tall, in pixels. Naboo's 400x444 view isn't,
  in pixels, though it's 1.43 on screen; both checks (`ProjectionProcessor`,
  `FramebufferRenderer`) now scale the width by the pixels' shape.

After: the street widens to 16:9 with the speeder the same size as in 4:3,
and the HUD (drawn in 3D in small viewports, which are rightly left alone,
and 2D rectangles) keeps its 4:3 layout in the middle, the radar's map back
in its ring.

**Walls missing in the wider view** (slots of sky between buildings, even
well inside the old 4:3 area). Widening only in RT64 means the game, and
the RSP's clipping, still see a 4:3 camera. So the camera's projection is
now widened game-side: `func_4001A078` builds it (matrix, fov, aspect, near,
far), and a hook multiplies the aspect by RT64's factor when the matrix is
the camera's (0x4007D9B0); RT64 is told not to widen it again
(`rt64_game_widens_projection`, `rt64_aspect_ratio_scale` in
`lib-patches/rt64/0026`). The view widened, but the slots stayed.

They were scissors. With the picture stretched (16:9 output, 4:3 camera)
there were none, and every triangle went through the 3D hooks, so it wasn't
RT64 losing them. `func_4000BB00` writes an RDP scissor from a rectangle in
-1..1 screen units, and giving it the whole screen made the slots go. The
rectangles are per cell of the level (array at the pointer in 0x40060054):
`func_410112B0` walks the level's portals from the camera, growing each
visible cell's rectangle, and the objects in a cell are drawn scissored to
it. It projects with its own numbers: cot(fov/2) at 0x40060060 and a
horizontal scale (1/aspect, from the 4:3 screen) at 0x40060064
(`func_41013DAC`: x * cot/z * [0x40060064] against the rectangle's x, y *
cot/z against its y). A hook just after it's set (0x41011648) divides that
scale by the same factor (`src/game/widescreen.cpp`), and the cell
rectangles match the wide camera: the street at the start of the first
mission now looks as in 4:3, with more of it at the sides.

**What's left at the sides: the level isn't built there.** In the mission's
opening cutscene a wall piece at the right edge was missing (sky through a
slot) as the camera panned. Not scissors (every scissor set to the whole
screen), not the portal walk (scale doubled), not the game's object tests
(`func_41013AD8`/`func_41013DAC` forced visible), not its aspect setting
(0x40075A7C), and not the microcode (no edge excess among the triangles it
drops that are on screen; its own RDP triangles miss it too): the game
never sends it. Widening the camera's fov instead (tan x1.4 on the view at
0x4004C67C, fov 45 in cutscenes, 75 in play) settled it: the zoomed-out view
shows the building just ending, sky and hills beyond, and no ground under
the street. The level is a set, built as far as the 4:3 camera sees.

So **cutscenes stay 4:3**. While the game is in its cinematic mode (the word
at 0x4005C84C is 7, from the mission intro until play starts) the camera and
portal walk aren't widened, and RT64 shows those frames at their original
shape with side bars (`rt64_keep_original_aspect`, carried in each workload
as `keepOriginalAspect` and applied in `threadConfigurationUpdate`,
`lib-patches/rt64/0026`; the game still sees the configured widening in
`rt64_aspect_ratio_scale`). They fill the window's width, cut off at the
top and bottom (`Present::fillWidth`, `VIRenderer::getViewportAndScissor`):
only letterbox is cut (the bars are 44 and 39 of 360 lines at 4:3, the zoom
takes 44 from each side at 16:9), so the picture's as large as it can be
with no sides to show. Play stays widescreen. Black areas remain at the
start of play where the camera begins beside unbuilt parts of the level, for
a second or two.

**Terrain at the sides in open levels** (user's clip of the swamp escort:
the hills end in jagged edges at the sides; with the user's save, which
starts at that level). Terrain is made only over the view's footprint on
the ground: `func_4104E8CC` takes the camera's eye and four far corners (at
+0x80 and +0x8C in the camera, 0x40060DBC), clips them to a height band
(`func_4104A1CC`) and fills the terrain grid row by row between them
(`func_4104A8C0`), hence the jagged edge. The corners come from
`func_41024064`, along the direction (-1.0, -0.7, 0.7 / tan(fov / 2)) at
sp + 0x50, the 4:3 TV shape (the logged corners were 1.43 wide to 1 high).
A hook after it's set (0x41024170) widens its x like the camera
(`naboo_widen_terrain`); the hills now reach both sides. (`func_410232A8`
and `func_410239E4`, which also read the camera's fov and viewport, were
widened first: they aren't it, three times wider changed nothing; the
microcode drops nothing at the edges, every triangle goes through the 3D
hooks.)

**A rare crash in level 2** (user: everything froze while flying). Here,
once in six five-minute runs: a segfault in `func_4000BB00` reading N64
address 0xA0000000, i.e. it was handed a cell rectangle pointer that isn't
in RAM (its callers do check for null). The DL chunk pool never ran low
(at least ~1000 of its chunks free). Cause not found; a hook before it now
replaces a rectangle pointer outside RAM with the whole screen's
(0x4004CD28) and reports it once (`[NABOO] func_4000BB00 given cell
rectangle at ...`), so the game carries on and the log shows the value.

**An expanded HUD** (the HUD setting did nothing: RT64 moves HUD pieces to
the sides only when a game anchors them with the extended GBI, which Naboo
doesn't). RT64 now anchors them itself while the game asks
(`rt64_auto_anchor_fb_width`, set to 400, the in-game framebuffer's width,
while the mode word at 0x4005C84C is 0; it's 5 paused, 7 cinematic):

- Texture rectangles without depth (the HUD's are; the world's sprites are
  depth tested) a third of the way in from either side are drawn anchored to
  that side (`RDP::drawTexRect`), given relative to it so they land where the
  game put them, with the scissor's sides anchored meanwhile (or the 4:3
  scissor cut them off). Their regions are kept, this frame and the last.
- The radar's ring is a 3D model drawn in front of the camera, with depth,
  in the full viewport; what places it is that its origin lands in the radar
  map's anchored region (`RDP::autoAnchorAt`). Such a model is moved across
  in clip space by as much as the rectangles are
  (`x += pct * (1 - 1/scale) * w`, `rt64_ext_aspect_percentage` from the HUD
  setting), so the ring stays on its map. Anchoring 3D models by position
  alone moved sky and world pieces (many are drawn without depth), and
  viewport anchors don't suit models in the full viewport.
- Not while paused: the pause menu's text is drawn a letter at a time, and
  letters near the sides came apart.

**Lag spikes, and render to RAM** (user's RT64 inspector: "Display List"
about 36 ms a frame, 22 ms of it CPU, everything else 1 ms or less). Timed
here: the interpreter itself is ~2 ms a task (~450k instructions at ~4.5 ns),
the 3D hooks under 1 ms, and almost all the rest (20-25 ms) is the RDP list
holding SYNC_FULL, i.e. `State::fullSync`. With RT64's default "render to
RAM" it renders every frame a second time at the original resolution and
waits for it, to write it to RDRAM. Nothing the game does reads a
framebuffer back from RAM (its framebuffer effects use RT64's GPU copies),
so the Naboo module turns it off (`state->setRenderToRAM(0)` in
`beginDisplayList`; `RT64_NABOO_RENDER_TO_RAM=1` turns it back on): ~11 ms a
task here instead of ~22, and title, menus, briefing, craft select, play,
pause, the crawl's text and cutscenes look the same either way. It may also
be the crashes: the game draws 448 lines into a framebuffer the VI shows as
444, so writing frames back to RDRAM went past its end.

Then a crash at craft select (user, D3D12): `D3D12CommandList::clearColor`
with no framebuffer bound, recording a fill rectangle on RT64's render thread.
Without the second render the game thread no longer waits for the renderer,
and craft select draws into a 512-wide framebuffer (play: 400), so a render
target being remade likely raced the renderer (its framebuffer storage is
dropped when a target is remade). Not reproducible here (Vulkan). So render to
RAM is now off only in play: the game's 400-wide framebuffer while it asks for
its HUD anchored (`rt64_auto_anchor_fb_width`); menus, craft select, pause and
cutscenes keep it, as before.

**Timing on players' machines** (user's inspector during a spike: Display
List 56 ms wall, ~25 ms CPU, shaders 72 to 74, GPU and workload small; then
a freeze). `NABOO_PERF=1` prints, every 2 s, each graphics task's time split
into the microcode (interpreter and 3D hooks), the RDP commands it hands
RT64, and RT64's end of frame (the list with SYNC_FULL), average and worst;
and a watchdog says which of those (or "between tasks", waiting for the
game) no task has left for 3 s. Here: task 5-8 ms (microcode 3-5, RDP 0.2,
end of frame 2-4).

**The freeze: the game stops, not the renderer** (user's NABOO_PERF log:
tasks 4-8 ms at 30 a second, the spikes all in RT64's end of frame, 96-216
ms now and then; then "no graphics task has finished ... stuck in between
tasks (waiting for the game)"). Two runtime changes
(`lib-patches/N64ModernRuntime/0011`):

- Each thread's host context is kept in a table outside game memory. The
  OSThread's context field overlays where libultra keeps a thread's saved
  registers, the game handles its OSThreads itself (it reads
  __osRunningThread in ~40 places to find its current process, whose struct
  holds the OSThread), and the earlier crash in `check_running_queue` read
  host address -1, i.e. a garbage host pointer from game memory. The field
  is still written and checked against the table: a mismatch is reported
  once per thread ("[ultramodern] Thread N's OSThread was overwritten"), and
  the table's pointer is used.
- `ultramodern::debug_dump_threads` prints every thread (priority, state,
  the queue it waits in with that queue's message counts) and the running
  queue; NABOO_PERF's watchdog calls it through `rt64_stall_callback`
  (set in `naboo_on_init`) when no graphics task has finished for 3 s.

**The corruption is the terrain widening** (user, `NABOO_NO_TERRAIN_WIDEN=1`:
24 minutes through level 3, no garbage pointer, no freeze or crash; with it,
level 2 failed within minutes). The microcode's DMA writes to RAM cover the
same ranges either way, so it's the game's own terrain code: the wider
footprint makes a level of detail span more of the grid (about 26 cells to
28-31) and something sized for the 4:3 footprint overflows; not found yet.
Terrain widening is now off unless `NABOO_TERRAIN_WIDEN=1`; terrain past the
4:3 sides is cut off again.

**Black screen after a failed mission's high scores** (buttons still
worked). The in-play flag (`rt64_auto_anchor_fb_width`) was set each time
the camera was built and never cleared, so the 2D screens after a mission
kept render to RAM off, and they show the last frame read back from RAM. The
game now also counts (`rt64_auto_anchor_serial`); a display list with no
count since the last one isn't in play: no anchoring, render to RAM on.

**Still open:** a column of stray pixels on 640-wide 2D screens ("A long
time ago...", mission complete), at about x = 620 of 640, in the black frames
between screens. Those frames are drawn by the RDP (a fill over the whole
buffer, scissor 0-639), not shown from RDRAM; where the column comes from
isn't known yet. And loud moments clip: the game's own mix reaches full
scale (the crawl's theme: up to 109 clipped samples a second), as it does on
mupen64plus (ref4.raw: 399 in its loudest second), so it's the game's, not
the port's audio path (no drops or underruns in the queue meanwhile).

**A dark line along the right edge** (and the bottom, at some window
sizes). The VI shows all 400 columns of the framebuffer (640 dots at 0.625),
but the game's scissor ends a column short, so the last column is never
drawn; on a TV it's in the overscan. Without borders, the VI renderer now
leaves off the framebuffer's last column and row (`lib-patches/rt64/0026`,
`VIRenderer::getViewportAndScissor`).

## 2026-10-31, round 21: noise at craft select (lost stream reads); a reference from mupen

**Loud noise at craft select** (still, after round 20). Recorded with
`NABOO_AUDIO_OUT`, the first seconds of craft select matched an emulator,
then a sound came in at RMS 11000-18000 (mupen: 3000-6000) with flat-topped
bursts at full scale, for 15 s or more.

A reference came first: `tools/mupen_play.c` plays the ROM on mupen64plus's
low-level RSP and video plugins with two plugins of ours
(`tools/mupen_script_plugins.c`): input that presses Start or A on chosen
frames, and audio that saves every AI buffer in `NABOO_AUDIO_OUT`'s format.
It can also save RDRAM at a frame. 10 presses from the title reach craft
select at about frame 7500 (the crawl isn't skippable there without a
save).

The trail, briefly: audio tasks from craft select give identical RAM on
rsp-z64 and the interpreter, so the RSP is right. The microcode reads a
buffer at `0x2D04A0` full of `0x8000` where mupen's holds zeros; a
watchpoint found it written by the hand-written routines at the end of
`.paged` (`func_410DA0A4` -> `func_410D9EE0`), the synthesis filter of the
speech decoder (`func_410D82A0`, which streams compressed speech into a
4 KB ring in 0x400-byte reads). Run in unicorn on the same input, the
original MIPS code of the filter, and of the whole frame decoder
(`func_410D8CE0`), gave the same output as the recompiled code. The input
was wrong: the ring held chunks out of order, one in about three never
read. The decoder then turned stale bytes into an unstable filter.

The speech player asks for a chunk through a callback (`func_400394C8`)
that queues the read on a list of four (`func_40039838`) and reports it
done at once. Each VI the sound engine's pass (`func_400376A8`) waits for
its last batch of reads (`func_40034D68`), starts a new batch
(`func_40034DFC`), moves the list into it (`func_40039510`, through the hook
at `0x400802DC`), renders the buffers the AI has played, and hands the batch
to the file loader (`func_40034DB0`). With nothing to render it jumps past
that last step, and the next pass starts a new batch: the reads were lost.
On the N64 the AI always takes a buffer or two between passes; here it can
take none in a VI (round 18's pacing, round 20's limit). A hook at that
jump's target (`0x40037998`, `naboo.us.rev0.toml`) hands over a batch that
holds reads and isn't already in flight. After: every chunk is read, in
order, and craft select measures 3000-5300 as on mupen, whether the crawl
is played or skipped.

Two dead ends, for the record: the file loader's 1 ms `osYieldThread` wait
(round 12) isn't the cause (reads were lost, not late), and the list of
four doesn't overflow.

**Start skips the opening crawl** (asked for, to test faster). The crawl
(`func_410A0B50`) checks A or Start each frame only if game flag 0x41 is
set (`func_410A6948(0x41)`, kept at `$sp+0xF0`), which a new save doesn't
have: it appears to be "seen it once". An instruction patch stores `$sp`
(never 0) there instead, so it's always skippable. Checked with no save: one
press at the "STAR WARS" title goes to craft select.

**Start skips the intro logos too.** The N64, LucasArts and Factor 5 logos
are level 20 (byte `0x4004CB86`), run by the scene loop (`func_4100096C`)
as a camera script (byte `0x4004CB8C`, mode 7). The game has no skip for
them: Start goes to the pause handler (`func_41001FC4`), which refuses
during a script. A hook at its entry asks the level to end in level 20
(`0x40096B88` bit 0, as the script does when the logos finish) and sets
`naboo_intro_skip` (`src/game/intro_skip.cpp`). The loop then still waits
for the script's time to run out, both before fading out (`0x41000F88`) and
before leaving (`0x41001028`); hooks there go ahead while the flag is set,
and drop it once the level has changed. One press at the N64 logo reaches
the title in about 3 s (the logos run to about 54 s from boot); the title
and menus behave as usual.

Not fixed: a few seconds of the cutscene before craft select are much louder
than on mupen (RMS ~16000 against ~5500, with clipping), with no reads lost
and no sign of the filter. Probably the "loud moments" of round 18.

## 2026-10-30, round 20: the squeal (stale audio buffers), the picture's shape, skipping intros

**A loud squeal at craft select and in missions** (and the crackle at loud
moments, round 18). `NABOO_AUDIO_OUT=file` now saves what the game hands the
AI. In it, a tenth of the 192-frame buffers were exact copies of the one 8
buffers before, in runs of one or two, and every buffer boundary was 1.5x
rougher than the inside of a buffer. The RSP side is right: 81 audio tasks
run through rsp-z64 (`ref_boot`) and the interpreter left identical RAM.

The game plays from a ring of 0x300-byte buffers (8 at its usual 22050-29000
Hz, 16 above; pointers at `0x40080330`, count at `0x40080328`). Its AI
thread (`func_40033E00`) queues the next one at each AI interrupt and keeps
the index at `0x4008032A`. Once a VI the sound engine (`func_400376A8`)
refills the ones played since, at most 3 or 4 (`0x40080302`), from its
cursor at `0x40090510`, and the scheduler runs the audio task that writes
them after the engine's *next* pass (logged: each task wrote the buffers up
to where the latest pass had started). On the N64 the AI takes 2-3 buffers
a VI, steadily, on the same clock as the VI. Here AI interrupts follow the
sound card (round 18, up to 3 a VI in bursts), and a game thread held up by
a long graphics task delays the engine: the AI reached buffers whose task
hadn't run and played what was left from 8 buffers earlier.

Fix: `lib-patches/N64ModernRuntime/0010` lets a game cap the AI interrupts
(`audio_callbacks_t::get_ai_events_allowed`, less those still waiting in
the game's queue). `src/game/audio_ring.cpp` allows only buffers an audio
task has written: a hook at the engine's pass (`naboo.us.rev0.toml`) notes
where the cursor starts, and `src/main/main.cpp` records that at each audio
task's start and publishes it at the end. If the game falls behind,
playback now waits (a short gap) instead of looping old audio. After: 8 exact
repeats in 9839 buffers (from 952 in 11611), and boundaries as smooth as
the rest (861 vs 843).

**The picture was nearly square.** RT64 takes a frame's original shape
(`aspectRatioSource`) from the framebuffer's pixels, square-pixel style:
400x444 here, 0.9:1, though the VI stretches them over 640 dots of a 4:3
screen. Everything after builds on it: the widescreen render target's width,
how far the 3D view is widened, the presented picture. A first try that only
widened the presented rectangle drew the picture smaller instead (the VI
pass maps the framebuffer at a fixed scale). `lib-patches/rt64/0026` adds
`VI::displayAspect()`, the shape the TV gives the VI's region ((hEnd -
hStart) of 640 dots across, (vEnd - vStart) of 474 half-lines down, so a
320x240 game stays 4:3; about 1.43 for Naboo), uses it as the frame's
source aspect, and stretches the presented picture across by the
framebuffer's pixel shape. With "expand" the 3D now fills a 16:9 window.
Full-screen 2D images (the title) are widened with it, as RT64 does.

**Skipping an intro hung the game** (a fade to black, during the LucasArts
logo or a mission's crawl; reported in round 19 as "skipping the LucasArts
logo"). The main thread (priority 10) waits in `func_40033584` for all of
the sound engine's voices to finish; the scheduler (`func_40022960`,
priority 117), which runs the sound engine each VI, was blocked for good in
an `osSendMesg` to the frame loop's tick queue (`0x400800E0`, one slot). It
sends the tick each VI while the flag at `0x4004D9F0` says the frame loop
waits for one (`func_400231C8`), blocking if the queue is full. The skip
stops the frame loop's thread as it waits, flag set, and the intro's manager
clears the flag later; here that came after a second tick, so the scheduler
blocked, the sound engine stopped, and the voices never finished. The send
is now `OS_MESG_NOBLOCK` (`naboo.us.rev0.toml`): a tick nobody waits for is
dropped. After: five presses during the LucasArts logo go to the title, and
key presses through a mission's crawl reach the mission.

## 2026-10-28, round 19: craft select, the black title, the launcher's cursor

**Craft select ended the game** ("Failed to find function at 0x410DA29C",
reproduced here by holding Start/A through the menus with xdotool). The
hand-written routines at the end of `.paged` (round 4) call small helpers
through `$t3`: `func_410DA0A4` sets `$t3` to `$ra` (from the `bltzal $zero`,
patched to a lui/addiu) plus an offset, and `func_410DA1F8` does `jalr $t3`
eight times. The four targets, `0x410DA29C`, `0x410DA2B4`, `0x410DA2C4` and
`0x410DA2DC` (four entry points into one routine that scales two values and
clamps the sum), were taken for data after `func_410DA1F8`. They're now
functions in the symbol file, each running to the shared `jr $ra` at
`0x410DA328`.

**The title screen was black** (music playing). Not the blanked mode copy
at `0x40080070` suspected last round: that's only set for a frame while the
game switches modes. Logging `osViSetMode` and `osViSwapBuffer` showed the
title sets `0x40080180` and swaps `0x8076F000`, as on mupen. That mode is
the 512x448 interlaced one: the VI width register is 1024 (two framebuffer
rows, so each field reads every other row) and field 0's origin is one row,
`0x400` bytes, in. RT64 halves the width for such modes (`VI::fbSize`) but
`VI::fbAddress` stepped back one row of the *VI* width, 2048 bytes, to
`0x8076EC00`: no framebuffer there, so RT64 showed RDRAM, which is near
empty (RT64 draws to the GPU). `lib-patches/rt64/0025` steps back a row of
the framebuffer's width. The intro's 400x448 mode (VI width 400) and all
non-interlaced modes are unchanged, since their row width is the VI's.

**The pointing-hand cursor stayed after the launcher.** RmlUi sets the
cursor for what's under the mouse, and nothing does once the menus close.
`src/main/main.cpp`'s `cursor_reset_watch` puts the arrow back on mouse
motion while no menu is shown.

**Not fixed (fixed in round 20): skipping the LucasArts logo.** A Start/A press during the
LucasArts snow scene fades it nearly to black and the game stays there (it
keeps drawing the dark scene). The same with real-time (SDL's dummy driver)
or no audio tasks, so it isn't the audio. Next: find what the intro's
process waits for after the fade (the game thread in `func_410A6778` waits
on a message queue; `func_41009EDC`'s thread is in `osStartThread`).

`NABOO_DUMP_AUDIO` takes a range, `n-m`.

## 2026-10-28, round 18: stuck after the Factor 5 logo: the audio microcode

**Not the graphics.** After the Factor 5 logo the screen stayed black with
the logo faintly on it. mupen64plus (with the low-level plugins) goes on to
the title screen, drawn into colour image `0x8076F000`; the port never got
there. gdb showed the main game thread spinning in `func_4100220C` (the
title screen's setup) on `func_40033584`, which returns 1 once every entry
of the sound engine's voice table (`*0x40080390`, 0x60 bytes each, active
flag at +8) has ended. Two voices from the logo never ended. (+0 is the
sound's length as a double, set when it starts in `func_40024060`.)

Two causes:

- **The spin never yields.** Game threads only switch at OS calls here, so
  the threads that end voices couldn't run. `func_40033584` has five
  callers, all `jal func_40033584; beq $v0, $zero, loop`; a hook where it
  computes its result (`0x40033600`) yields when a voice is still playing,
  as for round 12's frame wait.
- **The voices end from the audio microcode's results**, and the MusyX
  microcode recompiled by RSPRecomp (round 5) was never finished (it runs
  past the end of IMEM and through jump tables RSPRecomp couldn't follow),
  and `NABOO_NO_AUDIO` skips it altogether. Now the audio task runs in
  round 17's RSP interpreter (`src/game/rsp_audio.cpp`, using RT64's
  `RSPInterpreter`), starting from the task's own RSP boot code in IMEM 0
  as `osSpTaskLoad` leaves it, so the boot loads the microcode itself.
  `NABOO_AUDIO_RECOMP=1` uses the RSPRecomp version.

**A race between the two interpreters.** The first runs failed audio task
1083 or so (spinning until the instruction limit), though the same task
captured with `NABOO_DUMP_AUDIO_FAIL=1` ran fine offline and matched z64.
The vector unit reaches DMEM through one global pointer, set at the start
of each run, and graphics (RT64's thread) and audio (the RSP task thread)
now run at the same time, so one could load and store through the other's
DMEM. The pointer is thread-local now.

**Tested on Windows** (after the commit above): the game goes on past the
Factor 5 logo into the attract cutscene and the title screen's music, and
into a mission. Still: slow and stuttering, crackling audio, the title
screen black, models looking "tessellated", and a crash in a mission.

**Speed.** The interpreter now decodes IMEM into handler pointers the first
time each instruction runs (again where a DMA overwrites it: the overlays),
with a handler per vector function and element, so nothing switches at run
time; aligned scalar loads and stores and DMA rows are copied directly.
Same RDP output on every captured task, in about half the host
instructions (a heavy intro frame went from ~25 to ~13 ms here). The audio
task is about 1 ms. The one-time reciprocal table set-up starts its search
from the square root instead of 2^17.

**3D triangles by default.** The "tessellated" look is most likely the RDP
triangles RT64 draws in screen space (seams between them). The 3D path is
now the default (`RT64_NABOO_RDP_TRIANGLES=1` for the RDP triangles), with
RT64's interpolation turned off for its matrices (`matrixId` with every
component skipped) until the camera can be told apart: no detached limbs,
frames shown as the game makes them.

**Not reproduced here yet:** the black title screen (the title draws a
picture from RAM, `0x8021B420` up, as texture rectangles into a 512-wide
colour image at `0x8076F000`) and the crash in a mission (in the runtime's
`osEepromLongRead`, called from `func_410DA1F8`, by the crash log's best
guess). Under software rendering this machine is too slow to get there:
after the logo it sits in a load (`func_4000B258` waiting for a message).

**Crash logs name host functions.** For the mission crash: the Windows
build now writes line tables to a PDB (`-gline-tables-only` with clang-cl,
not for the recompiled game code), and crash_log.txt adds the host function
the fault is in and the host functions found on the stack (DbgHelp's
`SymFromAddr`; `dladdr` on Linux).

**Crackling audio.** `NABOO_AUDIO_QUEUE_STATS=1` prints how the output queue
looks each time the game hands over audio. The game delivers 192 frames
(~9 ms at 22050 Hz) at a time, only as fast as it plays, and 70% of the
time the queue was already empty: a gap each time. Playback now starts once
50 ms is queued and pauses to rebuild that if the queue runs dry. Here,
under software rendering, the game makes audio at 52% of real time, so it
still runs dry; on a machine running the game at full speed it shouldn't.
(A thread at `func_41009EDC` takes most of a core: it receives from the
queue at `0x40097108` and sends itself the next message, a background
worker the N64 runs in its idle time, so that's by design.)

**The real cause of the choppy audio: AI interrupts.** The game doesn't ask
`osAiGetLength`: its audio thread (`func_40033E00`) waits on the
`OS_EVENT_AI` queue and hands the AI one 192-frame buffer per message,
as the N64's AI raises its interrupt each time it starts a buffer. The
runtime raised one per VI, so 60 buffers a second: 11,520 frames against
22,050, the 52% measured, on any machine. `lib-patches/N64ModernRuntime/0009`
raises, at each VI, as many as it takes to bring the queued audio up to
75 ms, in the game's buffer size (at most three; the game's queue holds
four). Measured after: 100% of real time, the queue never empty, nothing
dropped.

**The crash at the craft selection** was the program exiting (the crash was
RmlUi's static destructors running under the game's threads); the game's
stack pointed at `osPfsReadWriteFile`. librecomp's fatal errors (a function
address it doesn't know, a `break`, a switch out of range) printed to
stderr, which a Windows release build doesn't have, and called `exit`.
`lib-patches/N64ModernRuntime/0008` shows them in a message box and uses
`quick_exit`.
The same crash was reported again with that in place, so something else
calls `exit()`. A normal quit ends with `_Exit`, so any `exit()` is the
runtime or a library giving up: an `atexit` handler registered just before
`recomp::start` (so it runs first) now writes the calls that led to it to
exit_log.txt, shows a message and ends the process before the static
destructors run.

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
