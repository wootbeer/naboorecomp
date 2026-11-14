// The file loader's manager thread (func_40004308) runs a loop while any request is pending:
// take new requests, move each of its eight request slots (0x74 bytes each, at 0x4007BE30) a
// step along, osYieldThread, again. The runtime's yield sleeps for up to 1 ms when nothing has
// happened since the thread's previous yield (lib-patches/N64ModernRuntime/0012), which it
// can't tell for this loop: a slot moving on is the loop's own work. This is called before the
// loop's yield and counts a change in the slots as activity, so a request that's moving isn't
// held up by a sleep at every step, and one that's waiting (a stream with nothing to read
// yet) still sleeps. STATUS.md round 26.
#include <atomic>
#include <cstdint>
#include <cstring>

#include "recomp.h"

extern std::atomic<uint32_t> ultramodern_activity;

extern "C" void naboo_loader_progress(uint8_t* rdram) {
    constexpr uint32_t slots = 0x0007BE30;
    constexpr uint32_t size = 8 * 0x74;
    static uint8_t last[size];
    if (std::memcmp(last, rdram + slots, size) != 0) {
        std::memcpy(last, rdram + slots, size);
        ultramodern_activity++;
    }
}

// Set by the scene's worker thread (func_41009EDC) when a pass of its loop brings something in
// (hooks in naboo.us.rev0.toml): a pass that didn't yields before the next.
extern "C" int naboo_worker_busy = 0;
