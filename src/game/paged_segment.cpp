// The game's second code segment, linked at 0x41000000 (ROM 0x6F370, 0xDA390
// bytes with its data). On the N64 it never sits in RAM as a whole: the
// game's TLB-miss handler (func_400008F4, stubbed) DMAs each 4 KB page from
// ROM into a small page pool the first time it's touched. Here the code is
// recompiled ahead of time, so only reads of its data (jump tables are in
// .main, but constants and tables live next to the code) need the bytes.
// They are loaded once at boot to where the runtime's address masking sends
// 0x41000000: physical 0x01000000 (lib-patches/N64Recomp/0001,
// lib-patches/N64ModernRuntime/0001). STATUS.md round 4.
#include "librecomp/addresses.hpp"
#include "librecomp/game.hpp"
#include "librecomp/helpers.hpp"
#include "recomp.h"
#include "ultramodern/ultramodern.hpp"

extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size);
extern "C" void unload_overlays(int32_t ram_addr, uint32_t size);

namespace {
    constexpr uint32_t main_rom = 0x1000;
    constexpr int32_t main_vram = 0x40000400;
    constexpr uint32_t paged_rom = 0x6F370;
    constexpr int32_t paged_vram = 0x41000000;
    constexpr uint32_t paged_size = 0xDA390;
}

// src/main/crash_handler.cpp: lets crash reports turn host addresses into N64 ones.
extern "C" void naboo_crash_note_rdram(uint8_t* rdram);

void naboo_on_init(uint8_t* rdram, recomp_context*) {
    naboo_crash_note_rdram(rdram);

    // The game reads libultra's __osRunningThread (0x4004ECD0) itself, about
    // 40 places, e.g. to find the current process (func_40006780). The
    // runtime switches threads on its own, so it keeps the variable up to
    // date (lib-patches/N64ModernRuntime/0004, STATUS.md round 9).
    ultramodern::set_running_thread_address((int32_t)0x4004ECD0);

    // The runtime registered every section in the first MB of ROM as if
    // IPL3 had loaded it there, which puts .paged's functions at
    // 0x4006E770 (inside .main's BSS). Move them to their real address so
    // function pointers into .paged resolve.
    unload_overlays(main_vram + (int32_t)(paged_rom - main_rom), paged_size);
    load_overlays(paged_rom, paged_vram, paged_size);

    recomp::do_rom_read(rdram, (gpr)paged_vram, recomp::rom_base + paged_rom, paged_size);
}
