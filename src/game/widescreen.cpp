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
// 4:3 and cut walls off in the wider picture.
#include <cstdint>
#include <cstring>

#include "recomp.h"

extern "C" float rt64_aspect_ratio_scale;
extern "C" bool rt64_game_widens_projection;

void naboo_widescreen_init() {
    rt64_game_widens_projection = true;
}

namespace {
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
    (void)rdram;
    const float scale = widescreen_scale();
    if (((uint32_t)ctx->r4 != 0x4007D9B0) || (scale == 1.0f)) {
        return;
    }

    ctx->r6 = (gpr)(int32_t)to_bits(to_float((uint32_t)ctx->r6) * scale);
}

extern "C" void naboo_widen_portals(uint8_t* rdram) {
    const float scale = widescreen_scale();
    if (scale == 1.0f) {
        return;
    }

    const gpr address = (gpr)(int32_t)0x40060064;
    MEM_W(0, address) = (int32_t)to_bits(to_float((uint32_t)MEM_W(0, address)) / scale);
}
