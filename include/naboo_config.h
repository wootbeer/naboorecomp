#ifndef __NABOO_CONFIG_H__
#define __NABOO_CONFIG_H__

#include <cstdint>

#include "librecomp/config.hpp"

// Game-specific options, shown on the General and Graphics tabs
// (src/main/game_config.cpp).
namespace naboo::config {
    // What's plugged into a controller's accessory slot. Like on the N64, one
    // controller holds one pak at a time.
    enum class Accessory : uint32_t {
        ControllerPak, // src/game/controller_pak_hle.cpp
        RumblePak,
        None,
    };

    // Adds this game's options to the General tab. Call between
    // recompui::config::create_general_tab() and recompui::config::finalize().
    void add_general_options(recomp::config::Config& general);

    // Screen Filter and Bloom at the end of the Graphics tab, passed straight
    // to RT64 (lib-patches/rt64/0018, 0019). Call between
    // recompui::config::create_graphics_tab() and recompui::config::finalize().
    void add_graphics_options(recomp::config::Config& graphics);

    Accessory get_accessory(int port);
    bool get_local_multiplayer();
}

#endif
