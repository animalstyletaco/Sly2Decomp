#pragma once

/*!
 * @file Graphics.h
 * Graphics component for the runtime. Abstraction layer for the main graphics routines.
 */

#include <array>
#include <functional>
#include <memory>

#include "Peripherals.h"

#include "common/versions.h"

 // forward declarations
struct GraphicsSettings;
class GraphicsDisplay;

// enum for rendering pipeline
enum class GraphicsPipeline { Invalid = 0, OpenGL = 1, Vulkan = 2};
enum GraphicsDisplayMode { Windowed = 0, Fullscreen = 1, Borderless = 2 };

// module for the different rendering pipelines
struct GraphicsRendererModule {
    std::function<int(GraphicsSettings&)> init;
    std::function<std::shared_ptr<GraphicsDisplay>(int width,
        int height,
        const char* title,
        GraphicsSettings& settings,
        GameVersion version,
        bool is_main)>
        make_display;
    std::function<void()> exit;
    std::function<uint32_t()> vsync;
    std::function<uint32_t()> sync_path;
    std::function<void(const void*, uint32_t)> send_chain;
    std::function<void(const uint8_t*, int, uint32_t)> texture_upload_now;
    std::function<void()> poll_events;
    std::function<void(float)> set_pmode_alp;
    GraphicsPipeline pipeline;
    const char* name;
};

// store settings related to the graphics systems
// TODO merge with globalsettings
struct GraphicsSettings {
    // current version of the settings. this should be set up so that newer versions are always higher
    // than older versions
    // increment this whenever you change this struct.
    // there's probably a smarter way to do this (automatically deduce size etc.)
    static constexpr uint64_t CURRENT_VERSION = 0x0000'0000'0004'0001;

    uint64_t Version;  // the version of this settings struct. MUST ALWAYS BE THE FIRST THING!

    Peripherals::MappingInfo PeripheralMappingInfo;         // button mapping
    Peripherals::MappingInfo PeripheralMappingInfoBackup;  // button mapping backup (see newpad.h)

    int vsync;   // (temp) number of screen update per frame
    bool IsDebugModeEnabled;  // graphics debugging

    GraphicsPipeline renderer = GraphicsPipeline::Invalid;  // which rendering pipeline to use.
};

struct GraphicsGlobalSettings {
    // note: this is actually the size of the display that ISN'T letterboxed
    // the excess space is what will be letterboxed away.
    int LetterBoxedWidth = 640;
    int LetterBoxedHeight = 480;

    // actual game resolution
    int GameResolutionWidth = 640;
    int GameResolutionHeight = 480;

    // multi-sampled anti-aliasing sample count. 1 = disabled.
    int MSAAsamples = 4;

    // current renderer
    const GraphicsRendererModule* renderer;

    // collision renderer settings
    bool IsCollisionEnable = false;
    bool IsCollisionWireFrame = true;

    // vsync enable
    bool IsVsyncEnabled = true;
    bool IsOldVsyncEnabled = false;
    // target frame rate
    float TargetFps = 60;
    // use custom frame limiter
    bool IsFrameLimiterEnabled = true;
};

namespace Graphics {

    extern GraphicsGlobalSettings g_global_settings;
    extern GraphicsSettings g_settings;

    const GraphicsRendererModule* GetCurrentRenderer();

    uint32_t Init(GameVersion version);
    void Loop(std::function<bool()> f);
    uint32_t Exit();

    Peripherals::MappingInfo& get_button_mapping();

    uint32_t vsync();
    uint32_t sync_path();
    void send_chain(const void* data, uint32_t offset);
    void texture_upload_now(const uint8_t* tpage, int mode, uint32_t s7_ptr);
    void texture_relocate(uint32_t destination, uint32_t source, uint32_t format);
    void set_levels(const std::vector<std::string>& levels);
    void poll_events();
    uint64_t get_window_width();
    uint64_t get_window_height();
    void set_window_size(uint64_t w, uint64_t h);
    void get_window_scale(float* x, float* y);
    GraphicsDisplayMode get_fullscreen();
    int get_screen_vmode_count();
    int get_screen_rate(int64_t vmode_idx);
    int get_monitor_count();
    void get_screen_size(int64_t vmode_idx, int32_t* w, int32_t* h);
    void set_frame_rate(int rate);
    void set_vsync(bool vsync);
    void set_letterbox(int w, int h);
    void set_fullscreen(GraphicsDisplayMode mode, int screen);
    void set_window_lock(bool lock);
    void set_game_resolution(int w, int h);
    void set_msaa(int samples);
    void input_mode_set(uint32_t enable);
    void input_mode_save();
    int64_t get_mapped_button(int64_t pad, int64_t button);
    bool get_debug_menu_visible_on_startup();

    int PadIsPressed(Peripherals::Button button, int port);
    int PadGetAnalogValue(Peripherals::Analog analog, int port);

}  // namespace Graphics