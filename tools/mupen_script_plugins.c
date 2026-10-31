// Two small mupen64plus plugins for tools/mupen_play.c, to get reference audio from
// an emulator (STATUS.md round 21). A development tool, not part of the game.
//
// Input (built with -DSCRIPT_INPUT): controller 1 holds Start or A for a few frames at
// the frames listed in NABOO_M64_PRESS ("3300:S,3480:A,..."); mupen_play sets the
// frame count through naboo_frame.
// Audio (built with -DCAPTURE_AUDIO): appends every buffer the game hands the AI to
// the file in NABOO_M64_AUDIO, as 16-bit stereo, left first (as NABOO_AUDIO_OUT does in
// the port), and prints the rate the game sets.
//   gcc -O1 -shared -fPIC -DSCRIPT_INPUT -o m64p_script_input.so tools/mupen_script_plugins.c
//   gcc -O1 -shared -fPIC -DCAPTURE_AUDIO -o m64p_capture_audio.so tools/mupen_script_plugins.c
#define M64P_PLUGIN_PROTOTYPES 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mupen64plus/m64p_types.h>
#include <mupen64plus/m64p_plugin.h>

EXPORT unsigned int naboo_frame = 0;

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle core, void *context, void (*debug)(void *, int, const char *)) {
    (void)core; (void)context; (void)debug;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void) {
    return M64ERR_SUCCESS;
}

EXPORT int CALL RomOpen(void) {
    return 1;
}

EXPORT void CALL RomClosed(void) {
}

#if defined(SCRIPT_INPUT)

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type, int *version, int *api, const char **name, int *caps) {
    if (type) *type = M64PLUGIN_INPUT;
    if (version) *version = 0x010000;
    if (api) *api = 0x020100;
    if (name) *name = "Naboo scripted input";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}

static unsigned presses[256];
static char buttons[256];
static int press_count = 0;

EXPORT void CALL InitiateControllers(CONTROL_INFO info) {
    info.Controls[0].Present = 1;
    info.Controls[0].RawData = 0;
    info.Controls[0].Plugin = PLUGIN_NONE;
    const char *list = getenv("NABOO_M64_PRESS");
    while (list != NULL && *list != '\0' && press_count < 256) {
        char *end;
        presses[press_count] = (unsigned)strtoul(list, &end, 0);
        buttons[press_count] = (*end == ':') ? end[1] : 'S';
        press_count++;
        list = strchr(end, ',');
        if (list != NULL) list++;
    }
}

EXPORT void CALL GetKeys(int control, BUTTONS *keys) {
    keys->Value = 0;
    if (control != 0) return;
    for (int i = 0; i < press_count; i++) {
        if (naboo_frame >= presses[i] && naboo_frame < presses[i] + 6) {
            if (buttons[i] == 'A') keys->A_BUTTON = 1;
            else keys->START_BUTTON = 1;
        }
    }
}

EXPORT void CALL ControllerCommand(int control, unsigned char *command) { (void)control; (void)command; }
EXPORT void CALL ReadController(int control, unsigned char *command) { (void)control; (void)command; }
EXPORT void CALL SDL_KeyDown(int keymod, int keysym) { (void)keymod; (void)keysym; }
EXPORT void CALL SDL_KeyUp(int keymod, int keysym) { (void)keymod; (void)keysym; }

#elif defined(CAPTURE_AUDIO)

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type, int *version, int *api, const char **name, int *caps) {
    if (type) *type = M64PLUGIN_AUDIO;
    if (version) *version = 0x010000;
    if (api) *api = 0x020000;
    if (name) *name = "Naboo audio capture";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}

static AUDIO_INFO audio;
static FILE *out;

EXPORT int CALL InitiateAudio(AUDIO_INFO info) {
    audio = info;
    const char *path = getenv("NABOO_M64_AUDIO");
    out = (path != NULL) ? fopen(path, "wb") : NULL;
    return 1;
}

EXPORT void CALL AiDacrateChanged(int system_type) {
    (void)system_type;
    // NTSC VI clock over (dacrate + 1).
    fprintf(stderr, "[capture] frame %u: AI rate %u Hz\n", naboo_frame, 48681812u / (*audio.AI_DACRATE_REG + 1));
}

EXPORT void CALL AiLenChanged(void) {
    if (out == NULL) return;
    const uint32_t addr = *audio.AI_DRAM_ADDR_REG & 0xFFFFFF;
    const uint32_t len = *audio.AI_LEN_REG & 0x3FFF8;
    // RDRAM is kept as 32-bit words in host order; each holds one frame, left in the
    // upper half.
    for (uint32_t i = 0; i + 4 <= len; i += 4) {
        uint32_t word;
        memcpy(&word, audio.RDRAM + addr + i, 4);
        const int16_t frame[2] = { (int16_t)(word >> 16), (int16_t)(word & 0xFFFF) };
        fwrite(frame, sizeof(frame), 1, out);
    }
    fflush(out);
}

EXPORT void CALL ProcessAList(void) {}
EXPORT void CALL SetSpeedFactor(int percent) { (void)percent; }
EXPORT void CALL VolumeUp(void) {}
EXPORT void CALL VolumeDown(void) {}
EXPORT int CALL VolumeGetLevel(void) { return 100; }
EXPORT void CALL VolumeSetLevel(int level) { (void)level; }
EXPORT void CALL VolumeMute(void) {}
EXPORT const char * CALL VolumeGetString(void) { return "100%"; }

#endif
