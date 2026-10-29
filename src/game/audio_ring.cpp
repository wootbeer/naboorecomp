// How far the AI may run ahead of the game's audio (ultramodern's get_ai_events_allowed,
// lib-patches/N64ModernRuntime/0010). STATUS.md round 20.
//
// The game plays from a ring of 0x300-byte buffers (8 at its usual rates, 16 above 22050 Hz;
// pointers from 0x40080330). Its AI thread (func_40033E00) queues the next one at each AI
// interrupt and keeps the index in 0x4008032A. Once a VI the sound engine (func_400376A8)
// refills the ones played since, 3 or 4 at most, from its cursor at 0x40090510 (the next
// to fill), and hands the audio task that writes them to the game's scheduler, which runs
// it after the engine's next pass: a task writes the buffers up to where that next pass
// started.
//
// On the N64 that keeps well ahead of the AI, which takes 2 or 3 buffers a VI, steadily:
// the AI and the VI share a clock. Here the AI follows the PC's sound card, in bursts, and
// the game's threads can be held up a VI or two (by a long graphics task); the AI then
// reached buffers not written yet and played them as they were, a loop of old audio heard
// as a squeal. So it may only queue buffers an audio task has written: if the game falls
// behind, playback waits for it (a short gap) instead.
#include <atomic>
#include <cstdint>
#include <limits>

#include "recomp.h"

namespace {
    constexpr uint32_t unknown = 0xFFFF;
    uint8_t* ring_rdram = nullptr;
    // Where the sound engine's latest pass started; at an audio task, where that task's
    // buffers end; and after it, the end of the buffers written.
    std::atomic<uint32_t> pass_start_cursor{ unknown };
    std::atomic<uint32_t> task_cursor{ unknown };
    std::atomic<uint32_t> written_cursor{ unknown };

    uint32_t read_cursor(uint8_t* rdram) {
        return MEM_HU(0, (gpr)(int32_t)0x40090510);
    }
}

void naboo_audio_ring_note_rdram(uint8_t* rdram) {
    ring_rdram = rdram;
}

extern "C" void naboo_audio_pass_start(uint8_t* rdram) {
    pass_start_cursor = read_cursor(rdram);
}

void naboo_audio_task_begin() {
    task_cursor = pass_start_cursor.load();
}

void naboo_audio_task_end() {
    written_cursor = task_cursor.load();
}

uint32_t naboo_ai_events_allowed() {
    constexpr uint32_t unlimited = std::numeric_limits<uint32_t>::max();
    uint8_t* rdram = ring_rdram;
    if (rdram == nullptr) {
        return unlimited;
    }

    const uint32_t count = MEM_HU(0, (gpr)(int32_t)0x40080328);
    const uint32_t written = written_cursor.load();
    if ((count == 0) || (read_cursor(rdram) >= count) || (written >= count)) {
        // Not set up yet (the cursor is 0xFFFF until the sound engine's first pass).
        return unlimited;
    }

    // The buffer at the index has been queued; the ones after it, up to the written
    // cursor, are ready.
    const uint32_t queued = MEM_BU(0, (gpr)(int32_t)0x4008032A);
    return (written + count - queued - 1) % count;
}
