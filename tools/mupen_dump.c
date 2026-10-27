// Reference RAM dumps from mupen64plus (STATUS.md round 13): runs the ROM in
// mupen64plus's core and saves the 8 MB of RDRAM every N frames, to compare
// the port's memory with the game running on an emulator.
//
// Linux, with mupen64plus and its development headers installed
// (apt install mupen64plus-ui-console mupen64plus-rsp-hle
//  mupen64plus-video-rice mupen64plus-input-sdl mupen64plus-audio-sdl
//  libmupen64plus-dev):
//   gcc -O1 -o mupen_dump tools/mupen_dump.c -ldl
//   SDL_AUDIODRIVER=dummy ./mupen_dump game.z64 dumps 100 1500 2
// NABOO_M64_GFX / NABOO_M64_RSP pick other plugins, e.g. the low-level
// mupen64plus-video-z64.so and mupen64plus-rsp-z64.so for reference pictures
// of Factor 5's microcode (apt install mupen64plus-video-z64
// mupen64plus-rsp-z64). An "every" of 0 dumps nothing.
// Dumps are in RDRAM's word-swapped layout (byte N at N ^ 3), as in the port.
// Rice can't draw Factor 5's microcode, so the game stalls after a few
// hundred frames; the CPU side is what this is for.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mupen64plus/m64p_types.h>
#include <mupen64plus/m64p_frontend.h>
#include <mupen64plus/m64p_debugger.h>
#include <mupen64plus/m64p_config.h>
#include <mupen64plus/m64p_common.h>

static ptr_CoreDoCommand doCmd; static ptr_DebugMemGetPointer memPtr;
static const char *outdir; static unsigned every, last;
static void dbg(void *ctx, int level, const char *msg) { if (level <= M64MSG_WARNING) fprintf(stderr, "%s: %s\n", (const char*)ctx, msg); }
static void frame_cb(unsigned int frame) {
    if (every > 0 && frame > 0 && frame % every == 0) {
        char path[512]; snprintf(path, sizeof path, "%s/f%05u.bin", outdir, frame);
        FILE *f = fopen(path, "wb"); fwrite(memPtr(M64P_DBG_PTR_RDRAM), 1, 0x800000, f); fclose(f);
        fprintf(stderr, "dumped %s\n", path);
    }
    if (frame >= last) doCmd(M64CMD_STOP, 0, NULL);
}
static m64p_dynlib_handle plugin(const char *name, m64p_dynlib_handle core, m64p_plugin_type type) {
    char path[256]; snprintf(path, sizeof path, "/usr/lib/x86_64-linux-gnu/mupen64plus/%s", name);
    m64p_dynlib_handle h = dlopen(path, RTLD_NOW); if (!h) { fprintf(stderr, "%s\n", dlerror()); exit(1); }
    ptr_PluginStartup st = (ptr_PluginStartup)dlsym(h, "PluginStartup");
    st(core, (void*)name, dbg);
    ptr_CoreAttachPlugin at = (ptr_CoreAttachPlugin)dlsym(core, "CoreAttachPlugin");
    if (at(type, h) != M64ERR_SUCCESS) fprintf(stderr, "attach %s failed\n", name);
    return h;
}
int main(int argc, char **argv) {
    outdir = argv[2]; every = atoi(argv[3]); last = atoi(argv[4]);
    int emumode = argc > 5 ? atoi(argv[5]) : 1;
    m64p_dynlib_handle core = dlopen("libmupen64plus.so.2", RTLD_NOW);
    ptr_CoreStartup cs = (ptr_CoreStartup)dlsym(core, "CoreStartup");
    doCmd = (ptr_CoreDoCommand)dlsym(core, "CoreDoCommand");
    memPtr = (ptr_DebugMemGetPointer)dlsym(core, "DebugMemGetPointer");
    cs(0x020001, NULL, "/usr/share/games/mupen64plus", "Core", dbg, NULL, NULL);
    ptr_ConfigOpenSection os = (ptr_ConfigOpenSection)dlsym(core, "ConfigOpenSection");
    ptr_ConfigSetParameter sp = (ptr_ConfigSetParameter)dlsym(core, "ConfigSetParameter");
    m64p_handle sec; os("Core", &sec); sp(sec, "R4300Emulator", M64TYPE_INT, &emumode);
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *rom = malloc(n); if (fread(rom, 1, n, f) != (size_t)n) return 1; fclose(f);
    if (doCmd(M64CMD_ROM_OPEN, n, rom) != M64ERR_SUCCESS) { fprintf(stderr, "rom open failed\n"); return 1; }
    const char *gfx = getenv("NABOO_M64_GFX"), *rsp = getenv("NABOO_M64_RSP");
    plugin(gfx ? gfx : "mupen64plus-video-rice.so", core, M64PLUGIN_GFX);
    plugin("mupen64plus-audio-sdl.so", core, M64PLUGIN_AUDIO);
    plugin("mupen64plus-input-sdl.so", core, M64PLUGIN_INPUT);
    plugin(rsp ? rsp : "mupen64plus-rsp-hle.so", core, M64PLUGIN_RSP);
    int zero = 0; doCmd(M64CMD_CORE_STATE_SET, M64CORE_SPEED_LIMITER, &zero);
    doCmd(M64CMD_SET_FRAME_CALLBACK, 0, (void*)frame_cb);
    doCmd(M64CMD_EXECUTE, 0, NULL);
    doCmd(M64CMD_ROM_CLOSE, 0, NULL);
    return 0;
}
