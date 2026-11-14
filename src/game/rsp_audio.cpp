// Factor 5's MusyX audio microcode, run in RT64's RSP interpreter (STATUS.md
// round 18). The RSPRecomp version (round 5) was never finished: the code runs
// past the end of IMEM and jumps through tables it couldn't follow. Interpreted,
// it runs as on the N64, starting from the task's own RSP boot code, which loads
// the microcode itself.
//
// The game depends on it beyond the sound: the sound engine ends voices from
// what the microcode reports, and the title screen waits for the Factor 5 logo's
// sounds to end.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "librecomp/rsp.hpp"
#include "gbi/rt64_rsp_interpreter.h"

// Headroom for the mix (STATUS.md round 26). The microcode adds its voices into 16-bit buffers
// with saturating adds, and the game's own levels pass full scale at loud moments (the crawl's
// theme peaks at 1.5 times full scale, as on an emulator), which is heard as crackle. Each
// voice's volumes are a ramp in the task's data: four 16.16 start values and four per-sample
// steps, at the start of each 0x50-byte voice (32 to a 0xA28-byte frame block, after its
// 0x28-byte header; the task's data size is the number of blocks). The microcode reads the
// voices with DMAs; the ramps are divided by 1 << naboo_audio_headroom_shift in the copy it
// gets (RDRAM is left alone: the game writes the next frame's voices while a task runs). The
// mix comes out that much quieter with nothing saturating, and queue_samples
// (src/main/main.cpp) makes up the gain in floating point, through a limiter.
// NABOO_AUDIO_HEADROOM=0 turns it off (0-3; 2 by default, 12 dB).
extern "C" int naboo_audio_headroom_shift = [] {
    const char* value = getenv("NABOO_AUDIO_HEADROOM");
    const int shift = (value != nullptr) ? atoi(value) : 2;
    return (shift < 0) ? 0 : ((shift > 3) ? 3 : shift);
}();

// What the last task's mix was divided by, as a shift (0 for a task whose ramps weren't found).
extern "C" int naboo_audio_applied_shift = 0;

namespace {
    constexpr uint32_t frame_block_size = 0xA28;
    constexpr uint32_t voices_offset = 0x28;
    constexpr uint32_t voice_size = 0x50;
    constexpr uint32_t ramp_bytes = 0x20;

    struct RampScaler {
        uint32_t data = 0;
        uint32_t end = 0;
        int shift = 0;
        uint32_t scaled = 0;
    };

    void scale_ramps_read(RT64::RSPInterpreter* rsp, uint32_t mem_address, uint32_t dram_address, uint32_t length, void* user_data) {
        RampScaler& scaler = *static_cast<RampScaler*>(user_data);
        if ((mem_address & 0x1000) != 0 || dram_address + length <= scaler.data || dram_address >= scaler.end) {
            return;
        }
        for (uint32_t i = 0; i + 4 <= length; i += 4) {
            const uint32_t address = dram_address + i;
            if (address < scaler.data || address >= scaler.end || (address & 3) != 0) {
                continue;
            }
            const uint32_t in_block = (address - scaler.data) % frame_block_size;
            if (in_block < voices_offset || ((in_block - voices_offset) % voice_size) >= ramp_bytes) {
                continue;
            }
            const int32_t value = static_cast<int32_t>(rsp->dmemWord(mem_address + i));
            rsp->setDMEMWord(mem_address + i, static_cast<uint32_t>(value >> scaler.shift));
            scaler.scaled++;
        }
    }
}

RspExitReason musyx_audio_interpreted(uint8_t* rdram, uint32_t ucode_addr) {
    (void)ucode_addr;
    static RT64::RSPInterpreter* rsp = nullptr;
    if (rsp == nullptr) {
        rsp = new RT64::RSPInterpreter();
    }

    // librecomp's run_task has put the OSTask at DMEM 0xFC0 (DMEM is kept word-swapped,
    // like RDRAM, so the task's words read as they are).
    rsp->dmem = dmem;
    rsp->rdram = rdram;
    rsp->rdramSize = 0x800000;

    // As osSpTaskLoad leaves things: the boot microcode at the start of IMEM, started there.
    const uint32_t boot_address = rsp->dmemWord(0xFC0 + 0x08) & 0x7FFFFF;
    const uint32_t boot_size = rsp->dmemWord(0xFC0 + 0x0C);
    memset(rsp->imem, 0, sizeof(rsp->imem));
    for (uint32_t i = 0; (i < boot_size) && (i < sizeof(rsp->imem)); i++) {
        rsp->imem[i ^ 3] = rdram[((boot_address + i) & 0x7FFFFF) ^ 3];
    }

    // NABOO_DUMP_AUDIO=n (or n-m) saves the n-th audio task (to the m-th: RAM and OSTask, as
    // NABOO_DUMP_GFX does) for tools/rsp_reference.c.
    static int task_count = 0;
    task_count++;
    static int dump_first = 0, dump_last = -1;
    static const bool dump_parsed = [] {
        if (const char* dump = getenv("NABOO_DUMP_AUDIO")) {
            dump_first = atoi(dump);
            const char* dash = strchr(dump, '-');
            dump_last = (dash != nullptr) ? atoi(dash + 1) : dump_first;
        }
        return true;
    }();
    (void)dump_parsed;
    if ((task_count >= dump_first) && (task_count <= dump_last)) {
        const char* dir_env = getenv("NABOO_DUMP_DIR");
        std::string base = std::string(dir_env != nullptr ? dir_env : ".") + "/aud_" + std::to_string(task_count);
        if (FILE* f = fopen((base + ".rdram").c_str(), "wb")) {
            for (uint32_t i = 0; i < 0x800000; i++) {
                fputc(rdram[i ^ 3], f);
            }
            fclose(f);
        }
        if (FILE* f = fopen((base + ".txt").c_str(), "w")) {
            fprintf(f, "audio task\n");
            for (uint32_t i = 0; i < 16; i++) {
                fprintf(f, "%08X%s", rsp->dmemWord(0xFC0 + i * 4), ((i % 4) == 3) ? "\n" : " ");
            }
            fclose(f);
        }
    }

    // NABOO_DUMP_AUDIO_FAIL=1 keeps a copy of RAM before each task and saves it for a task that fails.
    static const bool dump_fail = getenv("NABOO_DUMP_AUDIO_FAIL") != nullptr;
    static uint8_t* before = dump_fail ? new uint8_t[0x800000] : nullptr;
    static uint32_t task_words[16];
    if (dump_fail) {
        memcpy(before, rdram, 0x800000);
        for (uint32_t i = 0; i < 16; i++) {
            task_words[i] = rsp->dmemWord(0xFC0 + i * 4);
        }
    }

    rsp->reset();
    if (getenv("NABOO_AUDIO_CLEAR_DMEM") != nullptr) {
        memset(dmem, 0, 0xFC0);
    }

    static RampScaler scaler;
    const uint32_t blocks = rsp->dmemWord(0xFC0 + 0x34);
    scaler.data = rsp->dmemWord(0xFC0 + 0x30) & 0x7FFFFF;
    scaler.end = scaler.data + ((blocks <= 16) ? blocks : 0) * frame_block_size;
    scaler.shift = naboo_audio_headroom_shift;
    scaler.scaled = 0;
    rsp->dmaRead = (scaler.shift != 0) ? scale_ramps_read : nullptr;
    rsp->dmaReadUserData = &scaler;

    rsp->setPc(0);
    const RT64::RSPInterpreter::Result result = rsp->run(50000000ULL);
    // A task that read no ramps mixed no voices, so its (silent) output needs no gain either way.
    if (scaler.scaled != 0) {
        naboo_audio_applied_shift = scaler.shift;
    }
    else if (scaler.shift == 0) {
        naboo_audio_applied_shift = 0;
    }
    if (result != RT64::RSPInterpreter::Result::Broke) {
        fprintf(stderr, "The audio microcode didn't finish task %d (stopped at IMEM 0x%03X).\n", task_count, rsp->pc);
        if (dump_fail) {
            const char* dir_env = getenv("NABOO_DUMP_DIR");
            std::string base = std::string(dir_env != nullptr ? dir_env : ".") + "/audfail_" + std::to_string(task_count);
            if (FILE* f = fopen((base + ".rdram").c_str(), "wb")) {
                for (uint32_t i = 0; i < 0x800000; i++) {
                    fputc(before[i ^ 3], f);
                }
                fclose(f);
            }
            if (FILE* f = fopen((base + ".txt").c_str(), "w")) {
                fprintf(f, "audio task\n");
                for (uint32_t i = 0; i < 16; i++) {
                    fprintf(f, "%08X%s", task_words[i], ((i % 4) == 3) ? "\n" : " ");
                }
                fclose(f);
            }
        }

        return RspExitReason::Unsupported;
    }

    if (getenv("NABOO_AUDIO_STATS") != nullptr && (task_count % 100) == 0) {
        fprintf(stderr, "[audio] task %d done\n", task_count);
    }

    return RspExitReason::Broke;
}
