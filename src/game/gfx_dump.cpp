// Debugging aid for the graphics work: saves the N64's RAM (in the N64's own
// byte order) and the graphics task at chosen graphics tasks, to study the
// display lists offline. Off unless NABOO_DUMP_GFX lists task numbers, e.g.
//   NABOO_DUMP_GFX=20,60,150
// Files go to NABOO_DUMP_DIR (default: the working directory):
//   gfx_<n>.rdram  8 MB, big-endian, physical addresses
//   gfx_<n>.txt    the task's address and its OSTask words
// F12 also saves the next task, whatever the list (into the game's folder, or
// NABOO_DUMP_DIR).
// Hooked at the start of func_40023558, the game's RSP task launcher, which
// starts the task at *(OSTask**)0x40080120 (naboo.us.rev0.toml).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <set>
#include <string>
#include <vector>

#include "recomp.h"

namespace {
    std::set<int> wanted_tasks() {
        std::set<int> out;
        const char* list = std::getenv("NABOO_DUMP_GFX");
        if (list == nullptr) {
            return out;
        }
        std::string s{ list };
        size_t pos = 0;
        while (pos < s.size()) {
            size_t comma = s.find(',', pos);
            out.insert(std::atoi(s.substr(pos, comma - pos).c_str()));
            if (comma == std::string::npos) {
                break;
            }
            pos = comma + 1;
        }
        return out;
    }
}

static std::atomic<bool> dump_requested{ false };

extern "C" void naboo_request_gfx_dump() {
    dump_requested = true;
}

extern "C" void naboo_gfx_dump(uint8_t* rdram) {
    static const std::set<int> wanted = wanted_tasks();
    static int gfx_tasks = 0;

    constexpr gpr kCurrentTask = (gpr)(int32_t)0x40080120;
    gpr task = (gpr)(int32_t)MEM_W(0, kCurrentTask);
    if (MEM_W(0, task) != 1) { // M_GFXTASK
        return;
    }
    gfx_tasks++;
    if (!wanted.contains(gfx_tasks) && !dump_requested.exchange(false)) {
        return;
    }

    const char* dir_env = std::getenv("NABOO_DUMP_DIR");
    std::string dir = dir_env != nullptr ? dir_env : ".";
    std::string base = dir + "/gfx_" + std::to_string(gfx_tasks);

    std::vector<uint8_t> ram(0x800000);
    for (size_t i = 0; i < ram.size(); i++) {
        ram[i] = rdram[i ^ 3];
    }
    if (FILE* f = std::fopen((base + ".rdram").c_str(), "wb")) {
        std::fwrite(ram.data(), 1, ram.size(), f);
        std::fclose(f);
    }
    if (FILE* f = std::fopen((base + ".txt").c_str(), "w")) {
        std::fprintf(f, "task 0x%08X\n", (uint32_t)task);
        for (int i = 0; i < 16; i++) {
            std::fprintf(f, "%08X%s", (uint32_t)MEM_W(i * 4, task), (i % 4 == 3) ? "\n" : " ");
        }
        std::fclose(f);
    }
    std::fprintf(stderr, "[gfx dump] task %d (0x%08X) -> %s.rdram\n", gfx_tasks, (uint32_t)task, base.c_str());
}

