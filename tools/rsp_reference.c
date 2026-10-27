// Runs a captured graphics task on mupen64plus's low-level RSP plugin
// (rsp-z64, a real RSP interpreter) and saves the RDP commands the microcode
// produces: the reference for the RT64 implementation of Factor 5's
// microcode (STATUS.md round 17). A development tool, not part of the game.
//
// Linux (apt install libmupen64plus-dev mupen64plus-rsp-z64):
//   gcc -O1 -o rsp_reference tools/rsp_reference.c -ldl
//   ./rsp_reference gfx_600.rdram gfx_600.txt out.rdp [game.z64]
// The task comes from NABOO_DUMP_GFX (src/game/gfx_dump.cpp). The microcode
// text and data are read from the captured RAM (they're part of the game's
// code segment). out.rdp gets the RDP command stream as big-endian 64-bit
// words, in the order the RDP would take them.
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mupen64plus/m64p_types.h>
#include <mupen64plus/m64p_plugin.h>
#include <mupen64plus/m64p_common.h>

static unsigned char *rdram, dmem_imem[0x2000];
static unsigned int mi_intr, sp_mem_addr, sp_dram_addr, sp_rd_len, sp_wr_len, sp_status, sp_dma_full,
    sp_dma_busy, sp_pc, sp_semaphore, dpc_start, dpc_end, dpc_current, dpc_status, dpc_clock,
    dpc_bufbusy, dpc_pipebusy, dpc_tmem;
static FILE *out;
static unsigned long words, lists;

static uint8_t read_byte(uint32_t address) {
    // XBUS: the RDP reads from DMEM.
    if (dpc_status & 0x1) {
        return dmem_imem[(address & 0xFFF) ^ 3];
    }

    return rdram[(address & 0x7FFFFF) ^ 3];
}

// An RDP that finishes instantly: a pending new start (DPC_STATUS 0x400) takes
// effect, then everything up to the end is consumed.
static void process_rdp_list(void) {
    if (dpc_status & 0x400) {
        dpc_current = dpc_start;
        dpc_status &= ~0x400u;
    }

    for (uint32_t address = dpc_current; address + 8 <= dpc_end; address += 8) {
        for (uint32_t i = 0; i < 8; i++) {
            fputc(read_byte(address + i), out);
        }

        words++;
    }

    dpc_current = dpc_end;
    lists++;
}

static void nothing(void) {}

static uint32_t dmem_word(uint32_t address) {
    uint32_t value;
    memcpy(&value, &dmem_imem[address], 4);
    return value;
}

// With NABOO_RSP_TRACE set: what the microcode is up to, twice a second.
static void *monitor(void *arg) {
    (void)arg;
    for (;;) {
        usleep(500000);
        uint32_t dsum = 0, isum = 0;
        for (uint32_t i = 0; i < 0x1000; i++) {
            dsum = dsum * 31 + dmem_imem[i];
            isum = isum * 31 + dmem_imem[0x1000 + i];
        }

        fprintf(stderr, "dmem %08X imem %08X 140: %08X %08X 168: %08X | pc %03X dma %08X->%08X len %08X | ",
            dsum, isum, dmem_word(0x140), dmem_word(0x144), dmem_word(0x168), sp_pc, sp_dram_addr, sp_mem_addr, sp_rd_len);
        fprintf(stderr, "ring %08X..%08X at %08X | dpc %08X..%08X cur %08X status %08X | chunk %08X %08X | FD4 %08X FD8 %08X FDC %08X | lists %lu\n",
            dmem_word(0x110), dmem_word(0x114), dmem_word(0x118), dpc_start, dpc_end, dpc_current, dpc_status,
            dmem_word(0x270), dmem_word(0x274), dmem_word(0xFD4), dmem_word(0xFD8), dmem_word(0xFDC), lists);
    }

    return NULL;
}

static void debug_callback(void *context, int level, const char *message) {
    (void)context;
    if (level <= M64MSG_WARNING) {
        fprintf(stderr, "rsp: %s\n", message);
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s gfx_N.rdram gfx_N.txt out.rdp\n", argv[0]);
        return 1;
    }

    rdram = calloc(1, 0x800000);
    FILE *f = fopen(argv[1], "rb");
    if (f == NULL) {
        perror(argv[1]);
        return 1;
    }

    unsigned char *big = malloc(0x800000);
    size_t n = fread(big, 1, 0x800000, f);
    fclose(f);
    for (size_t i = 0; i < n; i++) {
        rdram[i ^ 3] = big[i];
    }

    uint32_t task[16];
    f = fopen(argv[2], "r");
    char line[256];
    if ((f == NULL) || (fgets(line, sizeof(line), f) == NULL)) {
        fprintf(stderr, "bad task file\n");
        return 1;
    }

    for (int i = 0; i < 16; i++) {
        if (fscanf(f, "%x", &task[i]) != 1) {
            fprintf(stderr, "bad task file\n");
            return 1;
        }
    }

    fclose(f);

    out = fopen(argv[3], "wb");
    void *plugin = dlopen("/usr/lib/x86_64-linux-gnu/mupen64plus/mupen64plus-rsp-z64.so", RTLD_NOW);
    if (plugin == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 1;
    }

    ((ptr_PluginStartup)dlsym(plugin, "PluginStartup"))(NULL, NULL, debug_callback);
    RSP_INFO info = {
        .RDRAM = rdram, .DMEM = dmem_imem, .IMEM = dmem_imem + 0x1000, .MI_INTR_REG = &mi_intr,
        .SP_MEM_ADDR_REG = &sp_mem_addr, .SP_DRAM_ADDR_REG = &sp_dram_addr, .SP_RD_LEN_REG = &sp_rd_len,
        .SP_WR_LEN_REG = &sp_wr_len, .SP_STATUS_REG = &sp_status, .SP_DMA_FULL_REG = &sp_dma_full,
        .SP_DMA_BUSY_REG = &sp_dma_busy, .SP_PC_REG = &sp_pc, .SP_SEMAPHORE_REG = &sp_semaphore,
        .DPC_START_REG = &dpc_start, .DPC_END_REG = &dpc_end, .DPC_CURRENT_REG = &dpc_current,
        .DPC_STATUS_REG = &dpc_status, .DPC_CLOCK_REG = &dpc_clock, .DPC_BUFBUSY_REG = &dpc_bufbusy,
        .DPC_PIPEBUSY_REG = &dpc_pipebusy, .DPC_TMEM_REG = &dpc_tmem,
        .CheckInterrupts = nothing, .ProcessDlistList = nothing, .ProcessAlistList = nothing,
        .ProcessRdpList = process_rdp_list, .ShowCFB = nothing,
    };
    unsigned int cycles = 0;
    ((ptr_InitiateRSP)dlsym(plugin, "InitiateRSP"))(info, &cycles);

    // (After InitiateRSP, which clears DMEM and IMEM.)
    // As the game starts a graphics task: the task header at DMEM 0xFC0, the
    // microcode's data at DMEM 0 (as N64ModernRuntime does), its first 0x1000
    // bytes of text in IMEM, starting at IMEM 0x1000.
    for (uint32_t i = 0; i < 0xF80; i++) {
        dmem_imem[i ^ 3] = rdram[((task[6] & 0x7FFFFF) + i) ^ 3];
    }

    for (uint32_t i = 0; i < 16; i++) {
        memcpy(&dmem_imem[0xFC0 + i * 4], &task[i], 4);
    }

    for (uint32_t i = 0; i < 0x1000; i++) {
        dmem_imem[(0x1000 + i) ^ 3] = rdram[((task[4] & 0x7FFFFF) + i) ^ 3];
    }


    ptr_DoRspCycles run = (ptr_DoRspCycles)dlsym(plugin, "DoRspCycles");
    if (getenv("NABOO_RSP_TRACE") != NULL) {
        pthread_t thread;
        pthread_create(&thread, NULL, monitor, NULL);
    }

    sp_pc = 0;
    sp_status = 0;
    int rounds = 0;
    while (!(sp_status & 0x1) && (rounds++ < 2000)) {
        run(10000);
        if ((rounds % 200) == 0 && getenv("NABOO_RSP_TRACE") != NULL) {
            fprintf(stderr, "round %d pc %03X status %08X dpc %08X..%08X cur %08X dpcstatus %08X dmaaddr %08X lists %lu\n",
                rounds, sp_pc, sp_status, dpc_start, dpc_end, dpc_current, dpc_status, sp_dram_addr, lists);
        }
    }

    fclose(out);
    printf("%s: status %08X after %d rounds, %lu RDP lists, %lu words\n", argv[1], sp_status, rounds, lists, words);
    return (sp_status & 0x1) ? 0 : 2;
}
