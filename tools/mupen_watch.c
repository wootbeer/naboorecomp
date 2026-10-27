// Finds the game code that writes an address, on mupen64plus (STATUS.md
// round 13): runs the ROM with the core's debugger on (pure interpreter, so
// slow) and prints the PC, $ra and $a0 of every write to [addr, addr + len),
// through both its 0x40xxxxxx and 0x80xxxxxx addresses.
//   gcc -O1 -o mupen_watch tools/mupen_watch.c -ldl
//   SDL_AUDIODRIVER=dummy ./mupen_watch game.z64 0x4004C68C 8 520
// See tools/mupen_dump.c for the packages it needs.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <mupen64plus/m64p_types.h>
#include <mupen64plus/m64p_frontend.h>
#include <mupen64plus/m64p_debugger.h>
#include <mupen64plus/m64p_config.h>
#include <mupen64plus/m64p_common.h>

static ptr_CoreDoCommand doCmd; static ptr_DebugMemGetPointer memPtr;
static ptr_DebugSetRunState setRun; static ptr_DebugStep step; static ptr_DebugGetCPUDataPtr cpuPtr;
static ptr_DebugBreakpointCommand bp; static ptr_DebugMemRead32 rd32;
static unsigned last, curFrame, hits;
static uint32_t watchAddr, watchLen;
static void dbg(void *ctx, int level, const char *msg) { if (level <= M64MSG_WARNING) fprintf(stderr, "%s: %s\n", (const char*)ctx, msg); }
static void frame_cb(unsigned int frame) { curFrame = frame; if (frame >= last) doCmd(M64CMD_STOP, 0, NULL); }
static void dinit(void) {}
static void dupdate(unsigned int pc) {
    int64_t *regs = (int64_t*)cpuPtr(M64P_CPU_REG_REG);
    if (hits++ < 400) {
        printf("frame %u pc %08X ra %08X a0 %08X val %08X %08X\n", curFrame, pc, (uint32_t)regs[31], (uint32_t)regs[4],
            rd32(watchAddr & ~3u), rd32((watchAddr & ~3u) + 4));
        fflush(stdout);
    }
    setRun(M64P_DBG_RUNSTATE_RUNNING);
    step();
}
static void dvi(void) {}
static m64p_dynlib_handle plugin(const char *name, m64p_dynlib_handle core, m64p_plugin_type type) {
    char path[256]; snprintf(path, sizeof path, "/usr/lib/x86_64-linux-gnu/mupen64plus/%s", name);
    m64p_dynlib_handle h = dlopen(path, RTLD_NOW); if (!h) { fprintf(stderr, "%s\n", dlerror()); exit(1); }
    ((ptr_PluginStartup)dlsym(h, "PluginStartup"))(core, (void*)name, dbg);
    ((ptr_CoreAttachPlugin)dlsym(core, "CoreAttachPlugin"))(type, h);
    return h;
}
int main(int argc, char **argv) {
    watchAddr = strtoul(argv[2], NULL, 0); watchLen = strtoul(argv[3], NULL, 0); last = atoi(argv[4]);
    m64p_dynlib_handle core = dlopen("libmupen64plus.so.2", RTLD_NOW);
    doCmd = (ptr_CoreDoCommand)dlsym(core, "CoreDoCommand");
    memPtr = (ptr_DebugMemGetPointer)dlsym(core, "DebugMemGetPointer");
    setRun = (ptr_DebugSetRunState)dlsym(core, "DebugSetRunState");
    step = (ptr_DebugStep)dlsym(core, "DebugStep");
    cpuPtr = (ptr_DebugGetCPUDataPtr)dlsym(core, "DebugGetCPUDataPtr");
    bp = (ptr_DebugBreakpointCommand)dlsym(core, "DebugBreakpointCommand");
    rd32 = (ptr_DebugMemRead32)dlsym(core, "DebugMemRead32");
    ((ptr_CoreStartup)dlsym(core, "CoreStartup"))(0x020001, NULL, "/usr/share/games/mupen64plus", "Core", dbg, NULL, NULL);
    ptr_ConfigOpenSection os = (ptr_ConfigOpenSection)dlsym(core, "ConfigOpenSection");
    ptr_ConfigSetParameter sp = (ptr_ConfigSetParameter)dlsym(core, "ConfigSetParameter");
    m64p_handle sec; os("Core", &sec);
    int zero = 0, one = 1;
    sp(sec, "R4300Emulator", M64TYPE_INT, &zero);
    sp(sec, "EnableDebugger", M64TYPE_BOOL, &one);
    ((ptr_DebugSetCallbacks)dlsym(core, "DebugSetCallbacks"))(dinit, dupdate, dvi);
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *rom = malloc(n); if (fread(rom, 1, n, f) != (size_t)n) return 1; fclose(f);
    if (doCmd(M64CMD_ROM_OPEN, n, rom) != M64ERR_SUCCESS) { fprintf(stderr, "rom open failed\n"); return 1; }
    plugin("mupen64plus-video-rice.so", core, M64PLUGIN_GFX);
    plugin("mupen64plus-audio-sdl.so", core, M64PLUGIN_AUDIO);
    plugin("mupen64plus-input-sdl.so", core, M64PLUGIN_INPUT);
    plugin("mupen64plus-rsp-hle.so", core, M64PLUGIN_RSP);
    m64p_breakpoint b;
    b.address = watchAddr; b.endaddr = watchAddr + watchLen - 1; b.flags = M64P_BKP_FLAG_ENABLED | M64P_BKP_FLAG_WRITE;
    bp(M64P_BKP_CMD_ADD_STRUCT, 0, &b);
    b.address = (watchAddr & 0x1FFFFFFF) | 0x80000000; b.endaddr = b.address + watchLen - 1;
    bp(M64P_BKP_CMD_ADD_STRUCT, 0, &b);
    doCmd(M64CMD_CORE_STATE_SET, M64CORE_SPEED_LIMITER, &zero);
    doCmd(M64CMD_SET_FRAME_CALLBACK, 0, (void*)frame_cb);
    setRun(M64P_DBG_RUNSTATE_RUNNING);
    doCmd(M64CMD_EXECUTE, 0, NULL);
    return 0;
}
