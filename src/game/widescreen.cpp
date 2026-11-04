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
#include <algorithm>
#include <cmath>
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
    // Testing switches: NABOO_NO_PORTAL_WIDEN=1 and NABOO_NO_TERRAIN_WIDEN=1 turn the portal
    // and terrain widening off.
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

extern "C" void naboo_heap_check(uint8_t* rdram, const char* place);

extern "C" void naboo_widen_projection(uint8_t* rdram, recomp_context* ctx) {
    if ((uint32_t)ctx->r4 != 0x4007D9B0) {
        return;
    }

    // Built once a frame, before its display list goes to RT64.
    naboo_heap_check(rdram, "the frame's start (func_4001A078)");
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

// The terrain's footprint: the camera's far corners (four points at +0x8C, 12 bytes apart) are
// a rectangle seen from the eye, 2.0 wide to 1.4 high in camera space. Each pair of corners on a
// horizontal edge is moved apart from its midpoint by the widescreen scale, which widens the
// rectangle's camera-space x exactly. This is done only while func_4104E8CC builds the terrain;
// the 4:3 corners are put back as it returns, so the other systems that read them (the camera,
// and func_40023BB0 from objects) see what they always did. Widening the corners for all of them
// (round 22) corrupted the game's memory in level 2 within minutes: a garbage cell rectangle
// pointer, then a freeze or crash in the thread queue. The terrain code's own writes, traced
// store by store, land in the same buffers with or without its wider footprint.
// NABOO_NO_TERRAIN_WIDEN=1 turns it off.
namespace {
    struct SavedCorners {
        uint32_t camera = 0;
        uint32_t original[12];
        uint32_t widened[12];
    } saved_corners;

    struct Vec3 { float x, y, z; };

    Vec3 corner(const uint32_t* words, int i) {
        return { to_float(words[i * 3 + 0]), to_float(words[i * 3 + 1]), to_float(words[i * 3 + 2]) };
    }

    float distance_sq(Vec3 a, Vec3 b) {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    bool in_ram(uint32_t address) {
        const uint32_t segment = address & 0xFF000000;
        return ((segment == 0x40000000) || (segment == 0x80000000)) && ((address & 0x00FFFFFF) < 0x800000 - 0x100) &&
            ((address & 3) == 0);
    }
}

extern "C" void naboo_widen_terrain(uint8_t* rdram, gpr camera) {
    naboo_heap_check(rdram, "the terrain's start (func_4104E8CC)");
    static const bool off = switched_off("NABOO_NO_TERRAIN_WIDEN");
    saved_corners.camera = 0;
    const uint32_t camera_address = (uint32_t)camera;
    if (off || !in_ram(camera_address)) {
        return;
    }

    const float scale = widescreen_scale();
    if (in_cutscene(rdram) || (scale == 1.0f)) {
        return;
    }

    const gpr base = (gpr)(int32_t)(camera_address + 0x8C);
    uint32_t words[12];
    for (int i = 0; i < 12; i++) {
        words[i] = (uint32_t)MEM_W(i * 4, base);
    }

    // The three ways to pair four corners: the rectangle's long sides (its width), its short
    // sides and its diagonals. The width's pairing is the one in between.
    static const int pairings[3][4] = { {0, 1, 2, 3}, {0, 3, 1, 2}, {0, 2, 1, 3} };
    float length[3];
    for (int p = 0; p < 3; p++) {
        const int* q = pairings[p];
        length[p] = distance_sq(corner(words, q[0]), corner(words, q[1])) + distance_sq(corner(words, q[2]), corner(words, q[3]));
        if (!(length[p] > 0.0f) || !std::isfinite(length[p])) {
            return;
        }
    }
    int order[3] = { 0, 1, 2 };
    std::sort(order, order + 3, [&](int a, int b) { return length[a] < length[b]; });
    const int* width_pairs = pairings[order[1]];
    // A rectangle's diagonals are as long as its sides together; anything else isn't the
    // footprint this expects, so it's left alone.
    const float sides = length[order[0]] + length[order[1]];
    if (std::fabs(length[order[2]] - sides) > 0.05f * sides) {
        return;
    }

    uint32_t widened[12];
    std::memcpy(widened, words, sizeof(widened));
    for (int pair = 0; pair < 2; pair++) {
        const int a = width_pairs[pair * 2], b = width_pairs[pair * 2 + 1];
        const Vec3 ca = corner(words, a), cb = corner(words, b);
        const Vec3 mid = { (ca.x + cb.x) * 0.5f, (ca.y + cb.y) * 0.5f, (ca.z + cb.z) * 0.5f };
        const Vec3 wa = { mid.x + (ca.x - mid.x) * scale, mid.y + (ca.y - mid.y) * scale, mid.z + (ca.z - mid.z) * scale };
        const Vec3 wb = { mid.x + (cb.x - mid.x) * scale, mid.y + (cb.y - mid.y) * scale, mid.z + (cb.z - mid.z) * scale };
        widened[a * 3 + 0] = to_bits(wa.x); widened[a * 3 + 1] = to_bits(wa.y); widened[a * 3 + 2] = to_bits(wa.z);
        widened[b * 3 + 0] = to_bits(wb.x); widened[b * 3 + 1] = to_bits(wb.y); widened[b * 3 + 2] = to_bits(wb.z);
    }

    for (int i = 0; i < 12; i++) {
        MEM_W(i * 4, base) = (int32_t)widened[i];
    }
    std::memcpy(saved_corners.original, words, sizeof(words));
    std::memcpy(saved_corners.widened, widened, sizeof(widened));
    saved_corners.camera = camera_address;
}

// Puts the 4:3 corners back, unless the camera has set new ones meanwhile (the terrain code can
// wait on the display list pool's lock, and other threads run then).
extern "C" void naboo_restore_terrain(uint8_t* rdram) {
    naboo_heap_check(rdram, "the terrain's end (func_4104E8CC)");
    if (saved_corners.camera == 0) {
        return;
    }
    const gpr base = (gpr)(int32_t)(saved_corners.camera + 0x8C);
    saved_corners.camera = 0;
    for (int i = 0; i < 12; i++) {
        if ((uint32_t)MEM_W(i * 4, base) != saved_corners.widened[i]) {
            return;
        }
    }
    for (int i = 0; i < 12; i++) {
        MEM_W(i * 4, base) = (int32_t)saved_corners.original[i];
    }
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
