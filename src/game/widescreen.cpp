// Widescreen for the 3D camera (STATUS.md round 22). The game builds its camera's projection
// at 0x4007D9B0 with func_4001A078 (fov, aspect, near, far), and the RSP's clipping follows
// that projection, so widening it in RT64 alone left things missing at the sides. The port
// widens the aspect it's built with by RT64's own factor (naboo.us.rev0.toml, hook at
// func_4001A078), and tells RT64 not to widen it again (lib-patches/rt64/0026). Other
// projections -- the HUD's, drawn in small viewports -- keep the game's 4:3.
//
// The level's portal walk (func_410112B0) has a projection of its own: it keeps a screen
// rectangle for each cell it can see into, and scissors what's drawn in the cell to it. Its
// horizontal scale (0x40060064) is narrowed by the same factor, or the cell rectangles stay
// 4:3 and cut walls off in the wider picture. Open levels' terrain is made for the view's
// footprint on the ground, from corners of its own (func_41024064, in the 4:3 TV shape),
// whose horizontal extent is widened the same way.
//
// Cutscenes stay 4:3. Their levels are built only as far as the 4:3 camera sees (past
// the sides there's sky, or nothing), so while the game is in its cinematic mode
// (0x4005C84C = 7) nothing is widened, and RT64 shows those frames at their original
// shape (rt64_keep_original_aspect, lib-patches/rt64/0026).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "recomp.h"

extern "C" float rt64_aspect_ratio_scale;
extern "C" bool rt64_game_widens_projection;
extern "C" bool rt64_keep_original_aspect;
extern "C" float rt64_fill_width_center;
extern "C" uint32_t rt64_auto_anchor_fb_width;
extern "C" uint32_t rt64_auto_anchor_serial;

void naboo_widescreen_init() {
    rt64_game_widens_projection = true;

    // Cutscenes fill the window's width, cutting off some of their letterbox. It isn't
    // centered (the picture is framebuffer lines 55-399 of 448), so neither is the cut.
    rt64_fill_width_center = 227.5f / 448.0f;
}

namespace {
    // Testing switches: NABOO_NO_PORTAL_WIDEN=1 turns the portal widening off, and
    // NABOO_TERRAIN_WIDEN=1 turns the terrain's on (it's off: see naboo_widen_terrain).
    bool switched_off(const char* name) {
        const char* value = std::getenv(name);
        return (value != nullptr) && (value[0] == '1');
    }

    bool in_cutscene(uint8_t* rdram) {
        return MEM_W(0, (gpr)(int32_t)0x4005C84C) == 7;
    }

    float widescreen_scale() {
        const float scale = rt64_aspect_ratio_scale;
        return (scale > 1.0f) ? scale : 1.0f;
    }

    float to_float(uint32_t bits) {
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    uint32_t to_bits(float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }
}

extern "C" void naboo_widen_projection(uint8_t* rdram, recomp_context* ctx) {
    if ((uint32_t)ctx->r4 != 0x4007D9B0) {
        return;
    }

    // Built once a frame, before its display list goes to RT64.
    const bool cutscene = in_cutscene(rdram);
    rt64_keep_original_aspect = cutscene;

    // In play (the mode word is 0; 5 paused, 7 cinematic) the HUD, drawn in the 400-wide
    // framebuffer, is anchored to the sides for the HUD setting to spread it out
    // (lib-patches/rt64/0026, rt64_auto_anchor_fb_width). Not paused: the pause menu's text
    // is drawn a letter at a time, and would come apart.
    rt64_auto_anchor_fb_width = (MEM_W(0, (gpr)(int32_t)0x4005C84C) == 0) ? 400 : 0;
    rt64_auto_anchor_serial++;
    const float scale = widescreen_scale();
    if (cutscene || (scale == 1.0f)) {
        return;
    }

    ctx->r6 = (gpr)(int32_t)to_bits(to_float((uint32_t)ctx->r6) * scale);
}

extern "C" void naboo_widen_portals(uint8_t* rdram) {
    static const bool off = switched_off("NABOO_NO_PORTAL_WIDEN");
    if (off) {
        return;
    }

    const float scale = widescreen_scale();
    if (in_cutscene(rdram) || (scale == 1.0f)) {
        return;
    }

    const gpr address = (gpr)(int32_t)0x40060064;
    MEM_W(0, address) = (int32_t)to_bits(to_float((uint32_t)MEM_W(0, address)) / scale);
}

// Off unless NABOO_TERRAIN_WIDEN=1: with it, level 2 (open terrain) corrupted the game's memory
// within minutes, a garbage cell rectangle pointer and then a freeze or a crash (a player's
// 24 minutes without it, through level 3, were clean). The wider footprint makes the terrain
// code cover more of its grid (the extents a level of detail spans grow from about 26 cells to
// 28-31) and something it fills is sized for the 4:3 footprint; not found yet. The microcode's
// own writes to RAM are the same either way. Without it, terrain past the 4:3 sides is cut off.
extern "C" void naboo_widen_terrain(uint8_t* rdram, gpr address) {
    static const bool on = switched_off("NABOO_TERRAIN_WIDEN");
    if (!on) {
        return;
    }

    const float scale = widescreen_scale();
    if (in_cutscene(rdram) || (scale == 1.0f)) {
        return;
    }

    MEM_W(0, address) = (int32_t)to_bits(to_float((uint32_t)MEM_W(0, address)) * scale);
}

// A cell rectangle the scissor is made from (func_4000BB00's a1) must be in RAM: KSEG0, or
// the mapped segments at 0x40000000/0x41000000 (all within the 8 MB). Otherwise the whole
// screen's rectangle is used instead, and the bad pointer reported once.
extern "C" void naboo_check_scissor_rect(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    const uint32_t rect = (uint32_t)ctx->r5;
    const uint32_t region = rect & 0xFF000000u;
    const bool in_ram = ((region == 0x80000000u) || (region == 0x40000000u) || (region == 0x41000000u)) && ((rect & 0x00FFFFFFu) < 0x00800000u) && ((rect & 3u) == 0);
    if (!in_ram) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            fprintf(stderr, "[NABOO] func_4000BB00 given cell rectangle at %08X (ra %08X), using the whole screen\n", rect, (uint32_t)ctx->r31);
        }
        ctx->r5 = (gpr)(int32_t)0x4004CD28;
    }
}
