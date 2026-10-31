// Plays the ROM on mupen64plus with scripted input and saves the audio the game makes,
// for reference against the port (STATUS.md round 21). A development tool, not part of
// the game. Uses the low-level RSP and video plugins (Factor 5's microcode needs them)
// and the two plugins in tools/mupen_script_plugins.c, built into the working folder.
//
//   gcc -O1 -o mupen_play tools/mupen_play.c -ldl
//   NABOO_M64_PRESS=3300:S,3480:S NABOO_M64_AUDIO=ref.raw NABOO_M64_SHOTS=3400,3600
//   (same line) SDL_AUDIODRIVER=dummy ./mupen_play game.z64 4000
// NABOO_M64_SHOTS lists frames to save the screen at (shot_NNNNN.png, through
// ImageMagick's import of the X display). NABOO_M64_DUMP=N saves RDRAM at frame N
// (ram_N.bin, in RDRAM's word-swapped layout, like tools/mupen_dump.c).
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mupen64plus/m64p_types.h>
#include <mupen64plus/m64p_frontend.h>
#include <mupen64plus/m64p_config.h>
#include <mupen64plus/m64p_debugger.h>
#include <mupen64plus/m64p_common.h>

static ptr_CoreDoCommand doCmd;
static ptr_DebugMemGetPointer memPtr;
static unsigned dump_frame;
static unsigned last;
static unsigned *frame_inputs, *frame_audio;
static unsigned shots[256];
static int shot_count;

static void dbg(void *ctx, int level, const char *msg) {
    if (level <= M64MSG_WARNING) fprintf(stderr, "%s: %s\n", (const char *)ctx, msg);
}

static void frame_cb(unsigned int frame) {
    *frame_inputs = frame;
    *frame_audio = frame;
    for (int i = 0; i < shot_count; i++) {
        if (shots[i] == frame) {
            char cmd[256];
            snprintf(cmd, sizeof cmd, "import -window root -resize 640x shot_%05u.png", frame);
            if (system(cmd) != 0) fprintf(stderr, "screenshot failed\n");
        }
    }
    if (dump_frame != 0 && frame == dump_frame) {
        char name[64];
        snprintf(name, sizeof name, "ram_%05u.bin", frame);
        FILE *out = fopen(name, "wb");
        fwrite(memPtr(M64P_DBG_PTR_RDRAM), 1, 0x800000, out);
        fclose(out);
    }
    if ((frame % 500) == 0) fprintf(stderr, "frame %u\n", frame);
    if (frame >= last) doCmd(M64CMD_STOP, 0, NULL);
}

static m64p_dynlib_handle plugin(const char *path, m64p_dynlib_handle core, m64p_plugin_type type) {
    m64p_dynlib_handle h = dlopen(path, RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); exit(1); }
    ((ptr_PluginStartup)dlsym(h, "PluginStartup"))(core, (void *)path, dbg);
    if (((ptr_CoreAttachPlugin)dlsym(core, "CoreAttachPlugin"))(type, h) != M64ERR_SUCCESS) {
        fprintf(stderr, "attach %s failed\n", path);
    }
    return h;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s game.z64 last_frame\n", argv[0]); return 1; }
    last = atoi(argv[2]);
    const char *list = getenv("NABOO_M64_SHOTS");
    while (list != NULL && *list != '\0' && shot_count < 256) {
        char *end;
        shots[shot_count++] = (unsigned)strtoul(list, &end, 0);
        list = (*end == ',') ? end + 1 : NULL;
    }

    m64p_dynlib_handle core = dlopen("libmupen64plus.so.2", RTLD_NOW);
    doCmd = (ptr_CoreDoCommand)dlsym(core, "CoreDoCommand");
    memPtr = (ptr_DebugMemGetPointer)dlsym(core, "DebugMemGetPointer");
    if (getenv("NABOO_M64_DUMP") != NULL) dump_frame = (unsigned)atoi(getenv("NABOO_M64_DUMP"));
    ((ptr_CoreStartup)dlsym(core, "CoreStartup"))(0x020001, NULL, "/usr/share/games/mupen64plus", "Core", dbg, NULL, NULL);
    m64p_handle sec;
    ((ptr_ConfigOpenSection)dlsym(core, "ConfigOpenSection"))("Core", &sec);
    int dynarec = 2;
    ((ptr_ConfigSetParameter)dlsym(core, "ConfigSetParameter"))(sec, "R4300Emulator", M64TYPE_INT, &dynarec);

    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *rom = malloc(n);
    if (fread(rom, 1, n, f) != (size_t)n) return 1;
    fclose(f);
    if (doCmd(M64CMD_ROM_OPEN, n, rom) != M64ERR_SUCCESS) { fprintf(stderr, "rom open failed\n"); return 1; }

    const char *dir = "/usr/lib/x86_64-linux-gnu/mupen64plus/";
    char path[512];
    snprintf(path, sizeof path, "%smupen64plus-video-z64.so", dir);
    plugin(path, core, M64PLUGIN_GFX);
    m64p_dynlib_handle audio = plugin("./m64p_capture_audio.so", core, M64PLUGIN_AUDIO);
    m64p_dynlib_handle input = plugin("./m64p_script_input.so", core, M64PLUGIN_INPUT);
    snprintf(path, sizeof path, "%smupen64plus-rsp-z64.so", dir);
    plugin(path, core, M64PLUGIN_RSP);
    frame_inputs = (unsigned *)dlsym(input, "naboo_frame");
    frame_audio = (unsigned *)dlsym(audio, "naboo_frame");

    int zero = 0;
    doCmd(M64CMD_CORE_STATE_SET, M64CORE_SPEED_LIMITER, &zero);
    doCmd(M64CMD_SET_FRAME_CALLBACK, 0, (void *)frame_cb);
    doCmd(M64CMD_EXECUTE, 0, NULL);
    doCmd(M64CMD_ROM_CLOSE, 0, NULL);
    return 0;
}
