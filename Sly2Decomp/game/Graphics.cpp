/*!
 * @file Graphics.cpp
 * Graphics component for the runtime. Abstraction layer for the main graphics routines.
 */

#include "Graphics.h"

#include <cstdio>
#include <functional>

#include "common/util/FileUtil.h"
#include "common/util/json_util.h"
#include "common/log/log.h"

#include "Display.h"
#include "opengl.h"

#include "Peripherals.h"
#include <thread>

std::thread::id g_main_thread_id = std::thread::id();

namespace {
    // initializes a Graphics settings.
    // TODO save and load from file
    void InitSettings(GraphicsSettings& settings) {
        // set the current settings version
        settings.Version = GraphicsSettings::CURRENT_VERSION;

        // use opengl by default for now
        settings.renderer = GraphicsPipeline::OpenGL;  // Graphics::renderers[0];

        // 1 screen update per frame
        settings.vsync = 1;

        // debug for now
        settings.IsDebugModeEnabled = true;

        // use buffered input mode
        settings.PeripheralMappingInfo.IsBufferedInputEnabled = true;
        // debug input settings
        settings.PeripheralMappingInfo.IsDebugModeEnabled = true;

        Peripherals::DefaultMapping(Graphics::g_settings.PeripheralMappingInfo);
    }

}  // namespace

namespace Graphics {

    GraphicsGlobalSettings g_global_settings;
    GraphicsSettings g_settings;

    Peripherals::MappingInfo& get_button_mapping() {
        return g_settings.PeripheralMappingInfo;
    }

    // const std::vector<const GraphicsRendererModule*> renderers = {&moduleOpenGL};

    // Not crazy about this declaration
    const std::pair<std::string, Peripherals::Button> gamepad_map[] = { {"Select", Peripherals::Button::Select},
                                                                        {"L3", Peripherals::Button::L3},
                                                                        {"R3", Peripherals::Button::R3},
                                                                        {"Start", Peripherals::Button::Start},
                                                                        {"Up", Peripherals::Button::Up},
                                                                        {"Right", Peripherals::Button::Right},
                                                                        {"Down", Peripherals::Button::Down},
                                                                        {"Left", Peripherals::Button::Left},
                                                                        {"L1", Peripherals::Button::L1},
                                                                        {"R1", Peripherals::Button::R1},
                                                                        {"Triangle", Peripherals::Button::Triangle},
                                                                        {"Circle", Peripherals::Button::Circle},
                                                                        {"X", Peripherals::Button::X},
                                                                        {"Square", Peripherals::Button::Square} };

    const std::pair<std::string, Peripherals::Analog> analog_map[] = {
        {"Left X Axis", Peripherals::Analog::Left_X},
        {"Left Y Axis", Peripherals::Analog::Left_Y},
        {"Right X Axis", Peripherals::Analog::Right_X},
        {"Right Y Axis", Peripherals::Analog::Right_Y},
    };

    bool g_is_debug_menu_visible_on_startup = false;

    bool get_debug_menu_visible_on_startup() {
        return g_is_debug_menu_visible_on_startup;
    }

    void DumpToJson(ghc::filesystem::path& filename) {
        nlohmann::json json;
        json["Debug Menu Visibility"] = false;  // Assume start up debug display is disabled
        auto& peripherals_json = json["Peripherals"];

        for (uint32_t i = 0; i < Peripherals::CONTROLLER_COUNT; ++i) {
            nlohmann::json peripheral_json;
            peripheral_json["ID"] = i + 1;

            auto& controller_json = peripheral_json["Controller"];
            auto& controller_buttons_json = controller_json["Buttons"];
            for (const auto& [name, value] : gamepad_map) {
                controller_buttons_json[name] =
                    g_settings.PeripheralMappingInfo.ControllerButtonMapping[i][(int)value];
            }

            auto& keyboard_json = peripheral_json["Keyboard+Mouse"];
            auto& keyboard_buttons_json = keyboard_json["Buttons"];
            for (const auto& [name, value] : gamepad_map) {
                keyboard_buttons_json[name] =
                    g_settings.PeripheralMappingInfo.KeyboardButtonMapping[i][(int)value];
            }

            auto& keyboard_analogs_json = keyboard_json["Analog"];
            for (const auto& [name, value] : analog_map) {
                if (g_settings.PeripheralMappingInfo.KeyboardAnalogMapping[i][(int)value].mode ==
                    Peripherals::AnalogMappingMode::AnalogInput) {
                    keyboard_analogs_json[name]["Axis Id"] =
                        g_settings.PeripheralMappingInfo.KeyboardAnalogMapping[i][(int)value].axis_id;
                }
                else {
                    keyboard_analogs_json[name]["Positive Key"] =
                        g_settings.PeripheralMappingInfo.KeyboardAnalogMapping[i][(int)value].positive_key;
                    keyboard_analogs_json[name]["Negative Key"] =
                        g_settings.PeripheralMappingInfo.KeyboardAnalogMapping[i][(int)value].negative_key;
                }
            }
            peripheral_json["X-Axis Mouse Sensitivity"] =
                g_settings.PeripheralMappingInfo.MouseXAxisSensitivities[i];
            peripheral_json["Y-Axis Mouse Sensitivity"] =
                g_settings.PeripheralMappingInfo.MouseYAxisSensitivities[i];
            peripherals_json.emplace_back(peripheral_json);
        }

        file_util::write_text_file(filename, json.dump(4));
    }

    void SavePeripheralSettings() {
        auto filename = (file_util::get_user_config_dir() / "controller" / "controller-settings.json");
        file_util::create_dir_if_needed_for_file(filename);

        DumpToJson(filename);
        lg::info("Saved graphics configuration file.");
    }

    void LoadPeripheralSettings(const ghc::filesystem::path& filepath) {
        Peripherals::DefaultMapping(g_settings.PeripheralMappingInfo);

        auto file_txt = file_util::read_text_file(filepath);
        auto configuration = parse_commented_json(file_txt, filepath.string());

        if (configuration.find("Debug Menu Visibility") != configuration.end()) {
            g_is_debug_menu_visible_on_startup = configuration["Debug Menu Visibility"].get<bool>();
        }

        int controller_index = 0;
        for (const auto& peripheral : configuration["Peripherals"]) {
            auto& controller_buttons_json = peripheral["Controller"]["Buttons"];
            auto& keyboard_buttons_json = peripheral["Keyboard+Mouse"]["Buttons"];

            for (const auto& [name, button] : gamepad_map) {
                if (controller_buttons_json.find(name) != controller_buttons_json.end()) {
                    g_settings.PeripheralMappingInfo.ControllerButtonMapping[controller_index][(int)button] =
                        controller_buttons_json[name].get<int>();
                }
                else {
                    lg::warn(
                        "Controller button override not found for {}. Using controller default value: {}", name,
                        g_settings.PeripheralMappingInfo.ControllerButtonMapping[controller_index][(int)button]);
                }

                if (keyboard_buttons_json.find(name) != keyboard_buttons_json.end()) {
                    g_settings.PeripheralMappingInfo.KeyboardButtonMapping[controller_index][(int)button] =
                        keyboard_buttons_json[name].get<int>();
                }
                else {
                    lg::warn(
                        "Keyboard button override not found for {}. Using keyboard default value: {}", name,
                        g_settings.PeripheralMappingInfo.KeyboardButtonMapping[controller_index][(int)button]);
                }
            }

            auto& keyboard_analogs_json = peripheral["Keyboard+Mouse"]["Analog"];
            for (const auto& [name, value] : analog_map) {
                Peripherals::AnalogMappingInfo analog_mapping;
                if (keyboard_analogs_json[name].contains("Axis Id") == true) {
                    analog_mapping.mode = Peripherals::AnalogMappingMode::AnalogInput;
                    analog_mapping.axis_id = keyboard_analogs_json[name]["Axis Id"].get<int>();
                    g_settings.PeripheralMappingInfo.KeyboardAnalogMapping[controller_index][(int)value] =
                        analog_mapping;
                    continue;
                }

                if (keyboard_analogs_json[name].contains("Positive Key") == true) {
                    analog_mapping.positive_key = keyboard_analogs_json[name]["Positive Key"].get<int>();
                }
                else {
                    lg::warn("Keyboard analog override not found for {}. Using keyboard default value: {}",
                        name,
                        g_settings.PeripheralMappingInfo.KeyboardAnalogMapping[controller_index][(int)value]
                        .positive_key);
                }

                if (keyboard_analogs_json[name].contains("Negative Key") == true) {
                    analog_mapping.negative_key = keyboard_analogs_json[name]["Negative Key"].get<int>();
                }
                else {
                    lg::warn("Keyboard analog override not found for {}. Using keyboard default value: {}",
                        name,
                        g_settings.PeripheralMappingInfo.KeyboardAnalogMapping[controller_index][(int)value]
                        .negative_key);
                }
                g_settings.PeripheralMappingInfo.KeyboardAnalogMapping[controller_index][(int)value] =
                    analog_mapping;
            }
            g_settings.PeripheralMappingInfo.MouseXAxisSensitivities[controller_index] =
                peripheral["X-Axis Mouse Sensitivity"].get<double>();
            g_settings.PeripheralMappingInfo.MouseYAxisSensitivities[controller_index] =
                peripheral["Y-Axis Mouse Sensitivity"].get<double>();
            controller_index++;
        }
    }

    void LoadSettings() {
        auto filename = (file_util::get_user_config_dir() / "controller" / "controller-settings.json");
        if (fs::exists(filename)) {
            LoadPeripheralSettings(filename);
            lg::info("Loaded graphics configuration file.");
            return;
        }
    }

    const GraphicsRendererModule* GetRenderer(GraphicsPipeline pipeline) {
        switch (pipeline) {
        case GraphicsPipeline::Invalid:
            lg::error("Requested invalid renderer {}", (u64)pipeline);
            return NULL;
        case GraphicsPipeline::OpenGL:
            return &gRendererOpenGL;
        default:
            lg::error("Requested unknown renderer {}", (u64)pipeline);
            return NULL;
        }
    }

    void SetRenderer(GraphicsPipeline pipeline) {
        g_global_settings.renderer = GetRenderer(pipeline);
        g_settings.renderer = pipeline;
    }

    const GraphicsRendererModule* GetCurrentRenderer() {
        return g_global_settings.renderer;
    }

    uint32_t Init(GameVersion version) {
        lg::info("Graphics Init");
        // initialize settings
        InitSettings(g_settings);
        // guarantee we have no keys detected by pad
        Peripherals::ForceClearKeys();

        LoadSettings();
        SetRenderer(g_settings.renderer);

        if (GetCurrentRenderer()->init(g_settings)) {
            lg::error("Graphics::Init error");
            return 1;
        }

        if (g_main_thread_id != std::this_thread::get_id()) {
            lg::error("Ran Graphics::Init outside main thread. Init display elsewhere?");
        }
        else {
            Display::InitMainDisplay(640, 480,
                fmt::format("Sly 2 Decompilation - Work in Progress").c_str(),
                g_settings, version);
        }

        return 0;
    }

    void Loop(std::function<bool()> f) {
        lg::info("Graphics Loop");
        while (f()) {
            // check if we have a display
            if (Display::GetMainDisplay()) {
                // lg::debug("run display");
                Display::GetMainDisplay()->render();
            }
        }
    }

    uint32_t Exit() {
        lg::info("Graphics Exit");
        Display::KillMainDisplay();
        GetCurrentRenderer()->exit();
        return 0;
    }

    uint32_t vsync() {
        if (GetCurrentRenderer()) {
            return GetCurrentRenderer()->vsync();
        }
        return 0;
    }

    uint32_t sync_path() {
        if (GetCurrentRenderer()) {
            return GetCurrentRenderer()->sync_path();
        }
        return 0;
    }

    void send_chain(const void* data, uint32_t offset) {
        if (GetCurrentRenderer()) {
            GetCurrentRenderer()->send_chain(data, offset);
        }
    }

    void texture_upload_now(const u8* tpage, int mode, uint32_t s7_ptr) {
        if (GetCurrentRenderer()) {
            GetCurrentRenderer()->texture_upload_now(tpage, mode, s7_ptr);
        }
    }

    void poll_events() {
        if (GetCurrentRenderer()) {
            GetCurrentRenderer()->poll_events();
        }
    }

    u64 get_window_width() {
        if (Display::GetMainDisplay()) {
            return Display::GetMainDisplay()->width();
        }
        else {
            return 0;
        }
    }

    u64 get_window_height() {
        if (Display::GetMainDisplay()) {
            return Display::GetMainDisplay()->height();
        }
        else {
            return 0;
        }
    }

    void set_window_size(u64 w, u64 h) {
        if (Display::GetMainDisplay()) {
            Display::GetMainDisplay()->set_size(w, h);
        }
    }

    void get_window_scale(float* x, float* y) {
        if (Display::GetMainDisplay()) {
            Display::GetMainDisplay()->get_scale(x, y);
        }
    }

    GraphicsDisplayMode get_fullscreen() {
        if (Display::GetMainDisplay()) {
            return Display::GetMainDisplay()->fullscreen_mode();
        }
        else {
            return GraphicsDisplayMode::Windowed;
        }
    }

    int get_screen_vmode_count() {
        if (Display::GetMainDisplay()) {
            return Display::GetMainDisplay()->get_screen_vmode_count();
        }
        return 0;
    }

    int get_screen_rate(s64 vmode_idx) {
        if (Display::GetMainDisplay()) {
            return Display::GetMainDisplay()->get_screen_rate(vmode_idx);
        }
        return 0;
    }

    int get_monitor_count() {
        if (Display::GetMainDisplay()) {
            return Display::GetMainDisplay()->get_monitor_count();
        }
        return 0;
    }

    void get_screen_size(s64 vmode_idx, s32* w, s32* h) {
        if (Display::GetMainDisplay()) {
            Display::GetMainDisplay()->get_screen_size(vmode_idx, w, h);
        }
    }

    void set_vsync(bool vsyncEnabled) {
        g_global_settings.IsVsyncEnabled = vsyncEnabled;
    }

    void set_frame_rate(int rate) {
        g_global_settings.TargetFps = rate;
        Peripherals::SetFrameRate(rate);
    }

    void set_letterbox(int width, int height) {
        g_global_settings.LetterBoxedWidth = width;
        g_global_settings.LetterBoxedHeight = height;
    }

    void set_fullscreen(GraphicsDisplayMode mode, int screen) {
        if (Display::GetMainDisplay()) {
            Display::GetMainDisplay()->set_fullscreen(mode, screen);
        }
    }

    void set_window_lock(bool lock) {
        if (Display::GetMainDisplay()) {
            Display::GetMainDisplay()->set_lock(lock);
        }
    }

    void set_game_resolution(int width, int height) {
        g_global_settings.GameResolutionWidth = width;
        g_global_settings.GameResolutionHeight = height;
    }

    void set_msaa(int samples) {
        g_global_settings.MSAAsamples = samples;
    }

    void input_mode_save() {
        SavePeripheralSettings();
    }

    s64 get_mapped_button(s64 pad, s64 button) {
        if (pad < 0 || pad > Peripherals::CONTROLLER_COUNT || button < 0 || button > 16) {
            lg::error("Invalid parameters to get_mapped_button({}, {})", pad, button);
            return -1;
        }

        return (Peripherals::GetGamepadState(pad) > -1)
            ? (s64)g_settings.PeripheralMappingInfo.ControllerButtonMapping[pad][button]
            : (s64)g_settings.PeripheralMappingInfo.KeyboardButtonMapping[pad][button];
    }

    int PadIsPressed(Peripherals::Button button, int port) {
        return Peripherals::IsPressed(g_settings.PeripheralMappingInfo, button, port);
    }

    int PadGetAnalogValue(Peripherals::Analog analog, int port) {
        return Peripherals::GetAnalogValue(g_settings.PeripheralMappingInfo, analog, port);
    }

}  // namespace Graphics