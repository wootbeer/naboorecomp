// What the game's own osInitialize (0x4003A108) sets up after the hardware
// work, which the runtime's replacement leaves out. Hooked into main()
// right after its osInitialize call (naboo.us.rev0.toml).
//
// - osClockRate (u64 at 0x4004EC40) starts at 62.5 MHz in the ROM and is
//   scaled by 3/4 to 46.875 MHz, the CPU count rate osGetTime() runs at.
//   Without it every timer that divides by osClockRate runs at 3/4 speed
//   (the BattleTanx: Global Assault port found this in its round 138).
// - osViClock (0x4004EC48): the VI clock for the TV type, 48,681,812 Hz
//   for NTSC, which the runtime reports (osTvType = 1).
//
// STATUS.md round 5.
#include <cstdint>

#include "recomp.h"

extern "C" void naboo_init_os(uint8_t* rdram) {
    constexpr gpr kOsClockRate = (gpr)(int32_t)0x4004EC40; // u64
    constexpr gpr kOsViClock = (gpr)(int32_t)0x4004EC48;   // u32

    uint64_t rate = (uint64_t((uint32_t)MEM_W(0, kOsClockRate)) << 32) | (uint32_t)MEM_W(4, kOsClockRate);
    rate = rate * 3 / 4;
    MEM_W(0, kOsClockRate) = (int32_t)(rate >> 32);
    MEM_W(4, kOsClockRate) = (int32_t)rate;

    MEM_W(0, kOsViClock) = 48681812;
}
