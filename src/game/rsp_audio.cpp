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

    // NABOO_DUMP_AUDIO=n saves the n-th audio task (RAM and OSTask, as NABOO_DUMP_GFX does) for
    // tools/rsp_reference.c.
    static int task_count = 0;
    task_count++;
    if (const char* dump = getenv("NABOO_DUMP_AUDIO"); (dump != nullptr) && (atoi(dump) == task_count)) {
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

    rsp->setPc(0);
    const RT64::RSPInterpreter::Result result = rsp->run(50000000ULL);
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

    return RspExitReason::Broke;
}
