// Command-line options: see include/naboo_command_line.h and `--help`.
#include "naboo_command_line.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

#include "librecomp/config.hpp"
#include "recompui/config.h"
#include "util/file.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
    using recomp::config::Config;
    using recomp::config::ConfigOption;
    using recomp::config::ConfigOptionType;
    using recomp::config::ConfigValueVariant;

    std::string lower(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }

    void set_environment(const char* name, const std::string& value) {
#if defined(_WIN32)
        _putenv_s(name, value.c_str());
#else
        setenv(name, value.c_str(), 1);
#endif
    }

    // Short names for the usual settings: `--<name> <value>` sets tab.option
    // (the values are the option's own, as --list-settings shows them).
    struct Alias {
        const char* name;
        const char* setting;
        const char* help;
    };

    const Alias value_aliases[] = {
        { "resolution", "graphics.res_option", "Rendering resolution" },
        { "aspect", "graphics.ar_option", "Aspect ratio" },
        { "hud-ratio", "graphics.hr_option", "HUD aspect ratio" },
        { "msaa", "graphics.msaa_option", "Anti-aliasing" },
        { "window-mode", "graphics.wm_option", "Windowed or fullscreen" },
        { "api", "graphics.api_option", "Graphics API" },
        { "downsample", "graphics.ds_option", "Downsampling" },
        { "screen-filter", "graphics.screen_filter", "Screen filter" },
        { "bloom", "graphics.bloom", "Bloom" },
        { "hp-framebuffer", "graphics.hpfb_option", "High precision framebuffer" },
        { "volume", "sound.main_volume", "Main volume, 0-100" },
        { "rumble", "general.rumble_strength", "Rumble strength, 0-100" },
        { "deadzone", "general.joystick_deadzone", "Joystick deadzone, 0-100" },
        { "background-input", "general.background_input_mode", "Controller input while the window isn't focused" },
        { "accessory", "general.p1_accessory", "Player 1's pak" },
        { "other-accessory", "general.other_accessory", "Players 2-4's pak" },
    };

    // Flags that set one setting to one value.
    struct FlagAlias {
        const char* name;
        const char* setting;
        const char* value;
        const char* help;
    };

    const FlagAlias flag_aliases[] = {
        { "fullscreen", "graphics.wm_option", "Fullscreen", "Start fullscreen" },
        { "windowed", "graphics.wm_option", "Windowed", "Start in a window" },
        { "developer-mode", "graphics.developer_mode", "true", "RT64's developer tools" },
        { "multiplayer", "general.local_multiplayer", "true", "Local multiplayer (up to 4 controllers)" },
        { "debug-mode", "general.debug_mode", "true", "The runtime's debug mode" },
    };

    // The game's own runtime switches (otherwise set as environment variables).
    struct EnvironmentFlag {
        const char* name;
        const char* variable;
        const char* value; // nullptr: the flag takes a value
        const char* help;
    };

    const EnvironmentFlag environment_flags[] = {
        { "no-frame-interpolation", "NABOO_INTERPOLATION", "0", "Show only the game's own frames, none generated in between" },
        { "smooth-frames", "NABOO_SMOOTH", "1", "Start with the experimental object smoothing on (F10 toggles it)" },
        { "perf", "NABOO_PERF", "1", "Print frame timing every 2 seconds" },
        { "audio-headroom", "NABOO_AUDIO_HEADROOM", nullptr, "Audio headroom, 0-3 (default 2)" },
        { "dump-dir", "NABOO_DUMP_DIR", nullptr, "Folder for F12 frame captures" },
        { "heap-check", "NABOO_HEAP_CHECK", nullptr, "Check the game's heap: 1 each frame, 2 on every allocation" },
    };

    struct TabConfig {
        const char* id;
        Config& (*get)();
    };

    const TabConfig tab_configs[] = {
        { "general", &recompui::config::get_general_config },
        { "graphics", &recompui::config::get_graphics_config },
        { "sound", &recompui::config::get_sound_config },
    };

    Config* find_config(const std::string& tab) {
        for (const TabConfig& entry : tab_configs) {
            if (lower(tab) == entry.id) {
                return &entry.get();
            }
        }
        return nullptr;
    }

    std::string value_text(const ConfigOption& option, const ConfigValueVariant& value) {
        switch (option.type) {
        case ConfigOptionType::Enum: {
            const auto& config = std::get<recomp::config::ConfigOptionEnum>(option.variant);
            if (const uint32_t* v = std::get_if<uint32_t>(&value)) {
                auto it = config.find_option_from_value(*v);
                if (it != config.options.end()) {
                    return it->key;
                }
                return std::to_string(*v);
            }
            break;
        }
        case ConfigOptionType::Number:
            if (const double* v = std::get_if<double>(&value)) {
                char text[64];
                std::snprintf(text, sizeof(text), "%g", *v);
                return text;
            }
            break;
        case ConfigOptionType::String:
            if (const std::string* v = std::get_if<std::string>(&value)) {
                return *v;
            }
            break;
        case ConfigOptionType::Bool:
            if (const bool* v = std::get_if<bool>(&value)) {
                return *v ? "true" : "false";
            }
            break;
        default:
            break;
        }
        return "?";
    }

    std::string accepted_values(const ConfigOption& option) {
        switch (option.type) {
        case ConfigOptionType::Enum: {
            const auto& config = std::get<recomp::config::ConfigOptionEnum>(option.variant);
            std::string text;
            for (const auto& entry : config.options) {
                text += (text.empty() ? "" : " | ") + entry.key;
            }
            return text;
        }
        case ConfigOptionType::Number: {
            const auto& config = std::get<recomp::config::ConfigOptionNumber>(option.variant);
            char text[96];
            if (config.max > config.min) {
                std::snprintf(text, sizeof(text), "number %g to %g", config.min, config.max);
            }
            else {
                std::snprintf(text, sizeof(text), "number");
            }
            return text;
        }
        case ConfigOptionType::String:
            return "text";
        case ConfigOptionType::Bool:
            return "true | false";
        default:
            return "?";
        }
    }

    bool parse_value(const ConfigOption& option, const std::string& text, ConfigValueVariant& out, std::string& error) {
        switch (option.type) {
        case ConfigOptionType::Enum: {
            const auto& config = std::get<recomp::config::ConfigOptionEnum>(option.variant);
            for (const auto& entry : config.options) {
                if ((lower(entry.key) == lower(text)) || (lower(entry.name) == lower(text))) {
                    out = entry.value;
                    return true;
                }
            }
            error = "takes " + accepted_values(option);
            return false;
        }
        case ConfigOptionType::Number: {
            const auto& config = std::get<recomp::config::ConfigOptionNumber>(option.variant);
            char* end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            if (text.empty() || (end == nullptr) || (*end != '\0')) {
                error = "takes a number";
                return false;
            }
            if ((config.max > config.min) && ((value < config.min) || (value > config.max))) {
                error = "takes " + accepted_values(option);
                return false;
            }
            out = value;
            return true;
        }
        case ConfigOptionType::String:
            out = text;
            return true;
        case ConfigOptionType::Bool: {
            const std::string value = lower(text);
            if ((value == "true") || (value == "on") || (value == "yes") || (value == "1")) {
                out = true;
                return true;
            }
            if ((value == "false") || (value == "off") || (value == "no") || (value == "0")) {
                out = false;
                return true;
            }
            error = "takes true or false";
            return false;
        }
        default:
            error = "can't be set from the command line";
            return false;
        }
    }

    // "--name=value" or "--name value".
    bool take_value(int argc, char** argv, int& i, const std::string& arg, const std::string& name, std::string& value, bool& missing) {
        const std::string flag = "--" + name;
        if (arg == flag) {
            if (i + 1 < argc) {
                value = argv[++i];
            }
            else {
                missing = true;
            }
            return true;
        }
        if (arg.rfind(flag + "=", 0) == 0) {
            value = arg.substr(flag.size() + 1);
            return true;
        }
        return false;
    }
}

naboo::cli::Options naboo::cli::parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        std::string value;
        bool missing = false;
        auto takes = [&](const std::string& name) { return take_value(argc, argv, i, arg, name, value, missing); };
        auto missing_value = [&]() { options.errors.push_back(arg + " needs a value"); };

        if ((arg == "--help") || (arg == "-h") || (arg == "/?")) {
            options.help = true;
        }
        else if (arg == "--list-settings") {
            options.list_settings = true;
        }
        else if ((arg == "--play") || (arg == "--boot")) {
            options.play = true;
        }
        else if (arg == "--save-settings") {
            options.save_settings = true;
        }
        else if (arg == "--portable") {
            options.portable = true;
        }
        else if (arg == "--no-portable") {
            options.no_portable = true;
        }
        // (A path from a std::string is read in the system code page on Windows, as argv is.)
        else if (takes("rom")) {
            if (missing) { missing_value(); continue; }
            options.rom = std::filesystem::path(value);
        }
        else if (takes("data-dir")) {
            if (missing) { missing_value(); continue; }
            options.data_dir = std::filesystem::path(value);
        }
        else if (takes("set")) {
            if (missing) { missing_value(); continue; }
            const size_t equals = value.find('=');
            if ((equals == std::string::npos) || (value.find('.') == std::string::npos) || (value.find('.') > equals)) {
                options.errors.push_back("--set takes tab.option=value, e.g. --set graphics.msaa_option=MSAA4X (got \"" + value + "\")");
                continue;
            }
            options.settings.emplace_back(value.substr(0, equals), value.substr(equals + 1));
        }
        else if (takes("game") || takes("game-mode")) {
            // The runtime's own: passed on as given.
            const std::string flag = arg.substr(0, arg.find('='));
            options.runtime_args.push_back(flag);
            if (!missing) {
                options.runtime_args.push_back(value);
            }
        }
        else if (takes("refresh-rate") || takes("framerate")) {
            if (missing) { missing_value(); continue; }
            // Display, Original, or a number of frames per second.
            const std::string rate = lower(value);
            char* end = nullptr;
            const double number = std::strtod(value.c_str(), &end);
            if ((rate == "display") || (rate == "original") || (rate == "manual")) {
                options.settings.emplace_back("graphics.rr_option", value);
            }
            else if (!value.empty() && (end != nullptr) && (*end == '\0') && (number > 0.0)) {
                options.settings.emplace_back("graphics.rr_option", "Manual");
                options.settings.emplace_back("graphics.rr_manual_value", value);
            }
            else {
                options.errors.push_back(arg.substr(0, arg.find('=')) + " takes display, original or a number (got \"" + value + "\")");
            }
        }
        else {
            bool known = false;
            for (const Alias& alias : value_aliases) {
                if (takes(alias.name)) {
                    known = true;
                    if (missing) { missing_value(); break; }
                    options.settings.emplace_back(alias.setting, value);
                    break;
                }
            }
            for (const FlagAlias& alias : flag_aliases) {
                if (!known && (arg == std::string("--") + alias.name)) {
                    known = true;
                    options.settings.emplace_back(alias.setting, alias.value);
                }
            }
            for (const EnvironmentFlag& flag : environment_flags) {
                if (known) {
                    break;
                }
                if (flag.value != nullptr) {
                    if (arg == std::string("--") + flag.name) {
                        known = true;
                        set_environment(flag.variable, flag.value);
                    }
                }
                else if (takes(flag.name)) {
                    known = true;
                    if (missing) { missing_value(); break; }
                    set_environment(flag.variable, value);
                }
            }
            if (!known) {
                options.errors.push_back("unknown option \"" + arg + "\" (see --help)");
            }
        }
    }

    if (options.play) {
        bool has_game = std::find(options.runtime_args.begin(), options.runtime_args.end(), "--game") != options.runtime_args.end();
        if (!has_game) {
            options.runtime_args.push_back("--game");
            options.runtime_args.push_back("naboo");
        }
    }

    return options;
}

void naboo::cli::attach_console() {
#if defined(_WIN32)
    static bool attached = false;
    if (attached) {
        return;
    }
    attached = true;
    // Already going somewhere (redirected to a file or pipe): leave it.
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out != nullptr) && (out != INVALID_HANDLE_VALUE) && (GetFileType(out) != FILE_TYPE_UNKNOWN)) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
        std::printf("\n");
    }
#endif
}

void naboo::cli::print_help() {
    std::printf(
        "Star Wars Episode I: Battle for Naboo, recompiled\n"
        "\n"
        "Usage: NabooRecompiled [options]\n"
        "\n"
        "Starting:\n"
        "  --play                         Go straight into the game (skips the launcher; needs a ROM)\n"
        "  --rom <file>                   Use this ROM (checked and stored, as the launcher's Select ROM does)\n"
        "  --game naboo                   The runtime's own form of --play\n"
        "\n"
        "Where settings, saves and mods are kept:\n"
        "  Portable by default: a new install keeps them in the folder it's started from\n"
        "  (marked by portable.txt there). If you already have them in the per-user folder\n"
        "  (%%LOCALAPPDATA%%\\naboorecomp on Windows, ~/.config/naboorecomp on Linux) they stay there;\n"
        "  delete portable.txt, or create one, to change.\n"
        "  --portable                     Keep them in the folder it's started from (creates portable.txt)\n"
        "  --no-portable                  On a first run, don't make the install portable\n"
        "  --data-dir <folder>            Use this folder for this launch\n"
        "\n"
        "Settings (for this launch; add --save-settings to keep them):\n"
        "  --set <tab.option>=<value>     Any setting: --list-settings shows them all with their values\n"
        "  --list-settings                List every setting, what it takes and its current value, and exit\n"
        "  --save-settings                Save the settings given here, as the menu's Apply does\n"
        "  --framerate <value>            Display, Original or a number of frames per second\n");
    for (const Alias& alias : value_aliases) {
        std::printf("  --%-28s %s (%s)\n", (std::string(alias.name) + " <value>").c_str(), alias.help, alias.setting);
    }
    for (const FlagAlias& alias : flag_aliases) {
        std::printf("  --%-28s %s\n", alias.name, alias.help);
    }
    std::printf("\nGame switches:\n");
    for (const EnvironmentFlag& flag : environment_flags) {
        std::printf("  --%-28s %s\n", (std::string(flag.name) + (flag.value == nullptr ? " <value>" : "")).c_str(), flag.help);
    }
    std::printf(
        "\n"
        "  --help                         This list\n"
        "\n"
        "Values aren't case sensitive, and an option's displayed name works too, e.g.\n"
        "  NabooRecompiled --rom \"Battle for Naboo.z64\" --play --fullscreen --msaa MSAA4X\n");
}

std::filesystem::path naboo::cli::choose_data_folder(const Options& options) {
    std::error_code error;
    if (options.data_dir) {
        std::filesystem::create_directories(*options.data_dir, error);
        return std::filesystem::absolute(*options.data_dir, error);
    }

    if (!std::filesystem::exists("portable.txt", error)) {
        bool make_portable = options.portable;
        if (!make_portable && !options.no_portable) {
            // Without portable.txt this is the per-user folder. Anything in it already: leave
            // that install as it is.
            const std::filesystem::path installed = recompui::file::get_app_folder_path();
            const bool existing = !installed.empty() && std::filesystem::exists(installed, error) &&
                !std::filesystem::is_empty(installed, error);
            make_portable = !existing;
        }

        if (make_portable) {
            std::ofstream marker("portable.txt");
            if (marker) {
                marker << "This file keeps Battle for Naboo Recompiled's settings, saves and mods in this folder.\n"
                          "Delete it to use the per-user folder instead.\n";
            }
            else {
                std::fprintf(stderr, "[NABOO] couldn't create portable.txt here; using the per-user folder.\n");
            }
        }
    }

    return recompui::file::get_app_folder_path();
}

bool naboo::cli::apply_settings(const Options& options) {
    bool ok = true;
    std::set<Config*> changed;
    for (const auto& [setting, text] : options.settings) {
        const size_t dot = setting.find('.');
        const std::string tab = setting.substr(0, dot);
        const std::string id = setting.substr(dot + 1);
        Config* config = find_config(tab);
        if (config == nullptr) {
            std::fprintf(stderr, "[NABOO] setting %s: no tab \"%s\" (general, graphics or sound)\n", setting.c_str(), tab.c_str());
            ok = false;
            continue;
        }
        if (!config->has_option(id)) {
            std::fprintf(stderr, "[NABOO] setting %s: the %s tab has no option \"%s\" (see --list-settings)\n", setting.c_str(), tab.c_str(), id.c_str());
            ok = false;
            continue;
        }

        const ConfigOption& option = config->get_option(id);
        ConfigValueVariant value;
        std::string error;
        if (!parse_value(option, text, value, error)) {
            std::fprintf(stderr, "[NABOO] setting %s: \"%s\" isn't a value it takes; it %s\n", setting.c_str(), text.c_str(), error.c_str());
            ok = false;
            continue;
        }

        // Into the settings in use (for a tab with an Apply button the value first goes to the
        // pending ones), with the option's change callbacks, as the menu does.
        config->set_option_value(id, value);
        if (config->get_option_value(id) != value) {
            // A tab with an Apply button: the value went to the pending settings.
            config->apply_option_value(id);
        }
        changed.insert(config);
        std::fprintf(stderr, "[NABOO] setting %s = %s\n", setting.c_str(), value_text(option, config->get_option_value(id)).c_str());
    }

    if (options.save_settings) {
        for (Config* config : changed) {
            config->save_config();
        }
    }

    return ok;
}

void naboo::cli::list_settings() {
    for (const TabConfig& entry : tab_configs) {
        Config& config = entry.get();
        std::printf("%s\n", entry.id);
        for (const ConfigOption& option : config.get_config_schema().options) {
            std::printf("  %s.%s = %s\n      %s; takes %s\n", entry.id, option.id.c_str(), value_text(option, config.get_option_value(option.id)).c_str(),
                option.name.c_str(), accepted_values(option).c_str());
        }
    }
    std::printf("\nSet one with --set tab.option=value (for this launch; add --save-settings to keep it).\n"
                "Controls are set in the game's Controls tab.\n");
}
