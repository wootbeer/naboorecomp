// Command-line options (src/main/command_line.cpp): where the data folder is
// (portable by default for a first run), the ROM, going straight into the
// game, every setting on the General, Graphics and Sound tabs, and the
// game's own runtime switches. `NabooRecompiled --help` lists them.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace naboo::cli {
    struct Options {
        bool help = false;
        bool list_settings = false;
        bool play = false;
        bool save_settings = false;
        bool portable = false;
        bool no_portable = false;
        std::optional<std::filesystem::path> rom;
        std::optional<std::filesystem::path> data_dir;
        // "tab.option" and the value given for it, in the order given.
        std::vector<std::pair<std::string, std::string>> settings;
        // What's passed on to the runtime (its own --game and --game-mode).
        std::vector<std::string> runtime_args;
        std::vector<std::string> errors;
    };

    // Reads the arguments; the game's runtime switches (--perf and so on) are
    // set as environment variables here, before anything reads them.
    Options parse(int argc, char** argv);

    // On Windows the release build has no console of its own: output goes to
    // the console it was started from, unless it's already redirected.
    void attach_console();

    void print_help();

    // The folder for settings, saves and mods. A portable.txt in the current
    // folder keeps everything there; on a first run (no data in the usual
    // per-user folder yet) one is created, so new installs are portable.
    // Anyone who already has data in the per-user folder stays there.
    std::filesystem::path choose_data_folder(const Options& options);

    // After the saved settings are loaded (recompui::config::finalize()).
    // Returns false if any setting couldn't be applied (reported on stderr).
    bool apply_settings(const Options& options);

    // Every setting: its tab and id, what values it takes and its current value.
    void list_settings();
}
