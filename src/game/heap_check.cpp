// Heap checker (NABOO_HEAP_CHECK=1): walks the game's heaps and reports the first block header
// that's been overwritten, to find what corrupts them (STATUS.md round 24).
//
// The heap (func_4000193C allocates, func_40001AFC frees) is a run of blocks, each a 16-byte
// header and its data: +0 the data's size, +8 flags (1 free, 2 used, 4 ?, 8 the end marker),
// +9 the allocation's tag. The next header is at header + 0x10 + size. Three heaps start at the
// pointers at 0x4007B178, 0x4007B164 and 0x4007B160 (0 when there isn't one).
//
// It's checked once a frame and as the terrain is built (entry and exit of func_4104E8CC), and
// with NABOO_HEAP_CHECK=2 also on every allocation. The first failure is printed with where it
// was found, the bad header, and the block before it (usually the one that ran over), once.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "recomp.h"

namespace {
    int check_level() {
        static const int level = []() {
            const char* value = std::getenv("NABOO_HEAP_CHECK");
            return (value != nullptr) ? std::atoi(value) : 0;
        }();
        return level;
    }

    uint32_t word(uint8_t* rdram, uint32_t phys) {
        return (uint32_t)MEM_W(0, (gpr)(int32_t)(0x80000000u | phys));
    }

    uint8_t byte(uint8_t* rdram, uint32_t phys) {
        return (uint8_t)MEM_BU(0, (gpr)(int32_t)(0x80000000u | phys));
    }

    void dump(uint8_t* rdram, const char* what, uint32_t phys, uint32_t length) {
        fprintf(stderr, "[NABOO HEAP]   %s at %08X:\n", what, 0x80000000u | phys);
        for (uint32_t a = phys; a < phys + length && a + 16 <= 0x800000; a += 16) {
            fprintf(stderr, "[NABOO HEAP]     %08X: %08X %08X %08X %08X\n", 0x80000000u | a,
                word(rdram, a), word(rdram, a + 4), word(rdram, a + 8), word(rdram, a + 12));
        }
    }

    bool reported = false;
    uint32_t good_checks = 0;
    const char* last_good_place = "none";

    // Returns false (and reports, once) if the heap starting at `head` is broken.
    uint32_t walked_blocks[3];

    bool walk(uint8_t* rdram, const char* place, int heap, uint32_t head) {
        walked_blocks[heap] = 0;
        uint32_t block = head & 0x1FFFFFFF;
        uint32_t previous = 0;
        for (int count = 0; count < 200000; count++) {
            const char* problem = nullptr;
            if (block < 0x400 || block + 16 > 0x800000 || (block & 7) != 0) {
                problem = "header outside RAM";
            }
            else {
                const uint32_t size = word(rdram, block);
                const uint8_t flags = byte(rdram, block + 8);
                if (flags & 8) {
                    walked_blocks[heap] = (uint32_t)count;
                    return true;
                }
                if ((flags & 0xF0) != 0 || (flags & 3) == 0 || (flags & 3) == 3) {
                    problem = "bad flags";
                }
                else if ((size & 0xF) != 0 || size >= 0x800000 || block + 16 + size + 16 > 0x800000) {
                    problem = "bad size";
                }
                if (problem == nullptr) {
                    previous = block;
                    block = block + 16 + size;
                    continue;
                }
            }
            if (!reported) {
                reported = true;
                fprintf(stderr, "[NABOO HEAP] heap %d (from %08X) broken, found at %s (last good check: %s, %u good checks): %s, block %08X\n",
                    heap, head, place, last_good_place, good_checks, problem, 0x80000000u | block);
                if (previous != 0) {
                    const uint32_t psize = word(rdram, previous);
                    fprintf(stderr, "[NABOO HEAP]   block before it: %08X, size %X, flags %02X, tag %02X\n",
                        0x80000000u | previous, psize, byte(rdram, previous + 8), byte(rdram, previous + 9));
                    dump(rdram, "its header", previous, 16);
                    const uint32_t end = previous + 16 + psize;
                    dump(rdram, "its last 0x80 bytes and the bad header", (end > previous + 0x90 ? end - 0x80 : previous + 16), 0xA0);
                }
                else if (block + 16 <= 0x800000) {
                    dump(rdram, "the bad header", block, 0x40);
                }
            }
            return false;
        }
        return true;
    }
}

extern "C" void naboo_heap_check(uint8_t* rdram, const char* place) {
    if (check_level() <= 0 || reported) {
        return;
    }
    static const uint32_t heads[3] = { 0x0007B178, 0x0007B164, 0x0007B160 };
    for (int i = 0; i < 3; i++) {
        const uint32_t head = word(rdram, heads[i]);
        if (head == 0) {
            walked_blocks[i] = 0;
            continue;
        }
        if (!walk(rdram, place, i, head)) {
            return;
        }
    }
    good_checks++;
    last_good_place = place;
    if ((good_checks % 5000) == 1) {
        fprintf(stderr, "[NABOO HEAP] %u checks fine (blocks in each heap: %u %u %u)\n", good_checks,
            walked_blocks[0], walked_blocks[1], walked_blocks[2]);
    }
}

extern "C" void naboo_heap_check_alloc(uint8_t* rdram) {
    if (check_level() >= 2) {
        naboo_heap_check(rdram, "an allocation (func_4000193C)");
    }
}
