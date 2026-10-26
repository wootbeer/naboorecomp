# Status

The project's working log: every investigation and fix, newest first, as
"rounds". PROGRESS.md has the short version of where things stand; this file
is for finding out why something is the way it is. Same format as the
BattleTanx: Global Assault port's STATUS.md, whose lessons are referred to as
"the BattleTanx port, round N".

## Overview (as of round 8)

**Where it stands.** The game's code recompiles cleanly and boots on Linux:
the launcher, the ROM check, osInitialize, the memory setup, all of the
game's threads, the VI and the video mode, and the first ROM reads. It then
stalls in its own file-loading thread (round 7). Nothing is drawn yet: the
game uses Factor 5's own graphics microcode, which RT64 doesn't support.

**How the main areas got here** (the rounds to read for each):

- **ROM layout and the TLB** (rounds 1-2). The code runs through the TLB at
  `0x40000000` and `0x41000000`; the second segment is paged in from ROM.
- **Symbols** (rounds 3-4). splat/spimdisasm split, libultra named from
  n64sym's signatures, and a generator that fixes the split
  (`tools/splat_to_syms.py`).
- **Recompiling** (round 4). N64Recomp and RSPRecomp fixes in
  `lib-patches/N64Recomp/`, the config in `naboo.us.rev0.toml`.
- **Runtime** (rounds 5-7). osInitialize's clock rate, the shims the game
  needs, osYieldThread, VI events before a video mode.

**Principle.** The game stays original: fixes cover only what differs from
the N64 (timing, rendering, the runtime), not the game's look or behaviour.

# Log

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
