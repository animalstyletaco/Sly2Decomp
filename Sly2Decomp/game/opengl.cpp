/*!
 * @file opengl.cpp
 * Lower-level OpenGL interface. No actual rendering is performed here!
 */

#include "opengl.h"

#include <condition_variable>
#include <memory>
#include <mutex>

#include "common/log/log.h"
#include "common/util/FileUtil.h"
#include "common/util/FrameLimiter.h"
#include "common/util/Timer.h"
#include "common/util/compress.h"

#include "Display.h"
#include "Graphics.h"
#include "DebugGui.h"
//#include "game/runtime.h"
#include "Peripherals.h"
#include "OpenGLRenderer.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "glad/glad.h"
#include "common/dma/dma_copy.h"

//Temp
constexpr int EE_MAIN_MEM_SIZE = 128 * (1 << 20);  // 128 MB, same as PS2 TOOL

namespace {

constexpr bool run_dma_copy = false;

struct GraphicsData {
  // vsync
  std::mutex sync_mutex;
  std::condition_variable sync_cv;

  // dma chain transfer
  std::mutex dma_mutex;
  std::condition_variable dma_cv;
  u64 frame_idx = 0;
  u64 frame_idx_of_input_data = 0;
  bool has_data_to_render = false;
  FixedChunkDmaCopier dma_copier;

  // temporary opengl renderer
  OpenGLRenderer ogl_renderer;

  OpenGlDebugGui debug_gui;

  FrameLimiter frame_limiter;
  Timer engine_timer;
  double last_engine_time = 1. / 60.;
  float pmode_alp = 0.f;

  std::string imgui_log_filename, imgui_filename;
  GameVersion version;

  GraphicsData();
  GraphicsData(GameVersion version) : version(version), dma_copier(EE_MAIN_MEM_SIZE) {
  }
};


enum class RuntimeExitStatus {
    RUNNING = 0,
    RESTART_RUNTIME = 1,
    EXIT = 2,
    RESTART_IN_DEBUG = 3,
};

// Set to nonzero to kill GOAL kernel - TEMP move to runtime code
RuntimeExitStatus MasterExit = RuntimeExitStatus::RUNNING;

std::unique_ptr<GraphicsData> g_graphics_data;

std::atomic<int> g_cursor_input_mode = GLFW_CURSOR_DISABLED;
bool is_cursor_position_valid = false;
double last_cursor_x_position = 0;
double last_cursor_y_position = 0;

struct {
  bool callbacks_registered = false;
  GLFWmonitor** monitors;
  int monitor_count;
} g_glfw_state;

void SetGlobalGLFWCallbacks() {
  if (g_glfw_state.callbacks_registered) {
    lg::warn("Global GLFW callbacks were already registered!");
  }

  // Get initial state
  g_glfw_state.monitors = glfwGetMonitors(&g_glfw_state.monitor_count);

  // Listen for events
  glfwSetMonitorCallback([](GLFWmonitor* /*monitor*/, int /*event*/) {
    // Reload monitor list
    g_glfw_state.monitors = glfwGetMonitors(&g_glfw_state.monitor_count);
  });

  g_glfw_state.callbacks_registered = true;
}

void ClearGlobalGLFWCallbacks() {
  if (!g_glfw_state.callbacks_registered) {
    return;
  }

  glfwSetMonitorCallback(NULL);

  g_glfw_state.callbacks_registered = false;
}

void ErrorCallback(int err, const char* msg) {
  lg::error("GLFW ERR {}: {}", err, std::string(msg));
}

bool HasError() {
  const char* ptr;
  if (glfwGetError(&ptr) != GLFW_NO_ERROR) {
    lg::error("glfw error: {}", ptr);
    return true;
  } else {
    return false;
  }
}

}  // namespace

static bool gl_inited = false;
static int gl_init(GraphicsSettings& settings) {
  if (glfwSetErrorCallback(ErrorCallback) != NULL) {
    lg::warn("glfwSetErrorCallback has been re-set!");
  }

  if (glfwInit() == GLFW_FALSE) {
    lg::error("glfwInit error");
    return 1;
  }

  // request an OpenGL 4.3 Core context
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);  // 4.3
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // core profile, not compat
  // debug check
  if (settings.IsDebugModeEnabled) {
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
  } else {
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_FALSE);
  }
  glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

  return 0;
}

static void gl_exit() {
  ClearGlobalGLFWCallbacks();
  g_graphics_data.reset();
  glfwTerminate();
  glfwSetErrorCallback(NULL);
  gl_inited = false;
}

static std::shared_ptr<GraphicsDisplay> gl_make_display(int width,
                                                   int height,
                                                   const char* title,
                                                   GraphicsSettings& settings,
                                                   GameVersion game_version,
                                                   bool is_main) {
  GLFWwindow* window = glfwCreateWindow(width, height, title, NULL, NULL);

  if (!window) {
    lg::error("gl_make_display failed - Could not create display window");
    return NULL;
  }

  glfwMakeContextCurrent(window);
  if (!gl_inited) {
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    if (!gladLoadGL()) {
      lg::error("GL init fail");
      return NULL;
    }
    g_graphics_data = std::make_unique<GraphicsData>(game_version);

    gl_inited = true;
  }

  SetGlobalGLFWCallbacks();
  Peripherals::initialize();

  if (HasError()) {
    lg::error("gl_make_display error");
    return NULL;
  }

  auto display = std::make_shared<OpenGLDisplay>(window, is_main);

  display->set_imgui_visible(Graphics::get_debug_menu_visible_on_startup());
  display->update_cursor_visibility(window, display->is_imgui_visible());
  glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);

  // lg::debug("init display #x{:x}", (uintptr_t)display);

  // setup imgui

  // check that version of the library is okay
  IMGUI_CHECKVERSION();

  // this does initialization for stuff like the font data
  ImGui::CreateContext();

  // Init ImGui settings
  g_graphics_data->imgui_filename = file_util::get_file_path({"imgui.ini"});
  g_graphics_data->imgui_log_filename = file_util::get_file_path({"imgui_log.txt"});
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = g_graphics_data->imgui_filename.c_str();
  io.LogFilename = g_graphics_data->imgui_log_filename.c_str();

  // set up to get inputs for this window
  ImGui_ImplGlfw_InitForOpenGL(window, true);

  // NOTE: imgui's setup calls functions that may fail intentionally, and attempts to disable error
  // reporting so these errors are invisible. But it does not work, and some weird X11 default
  // cursor error is set here that we clear.
  glfwGetError(NULL);

  // set up the renderer
  ImGui_ImplOpenGL3_Init("#version 430");

  return std::static_pointer_cast<GraphicsDisplay>(display);
}

OpenGLDisplay::OpenGLDisplay(GLFWwindow* window, bool is_main) : m_window(window) {
  m_main = is_main;

  // Get initial state
  get_position(&m_last_windowed_xpos, &m_last_windowed_ypos);
  get_size(&m_last_windowed_width, &m_last_windowed_height);

  // Listen for window-specific GLFW events
  glfwSetWindowUserPointer(window, reinterpret_cast<void*>(this));

  glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
    OpenGLDisplay* display = reinterpret_cast<OpenGLDisplay*>(glfwGetWindowUserPointer(window));
    display->on_key(window, key, scancode, action, mods);
  });

  glfwSetMouseButtonCallback(window, [](GLFWwindow* window, int button, int action, int mode) {
    OpenGLDisplay* display = reinterpret_cast<OpenGLDisplay*>(glfwGetWindowUserPointer(window));
    display->on_mouse_key(window, button, action, mode);
  });

  glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xposition, double yposition) {
    OpenGLDisplay* display = reinterpret_cast<OpenGLDisplay*>(glfwGetWindowUserPointer(window));
    display->on_cursor_position(window, xposition, yposition);
  });

  glfwSetWindowPosCallback(window, [](GLFWwindow* window, int xpos, int ypos) {
    OpenGLDisplay* display = reinterpret_cast<OpenGLDisplay*>(glfwGetWindowUserPointer(window));
    display->on_window_pos(window, xpos, ypos);
  });

  glfwSetWindowSizeCallback(window, [](GLFWwindow* window, int width, int height) {
    OpenGLDisplay* display = reinterpret_cast<OpenGLDisplay*>(glfwGetWindowUserPointer(window));
    display->on_window_size(window, width, height);
  });

  glfwSetWindowIconifyCallback(window, [](GLFWwindow* window, int iconified) {
    OpenGLDisplay* display = reinterpret_cast<OpenGLDisplay*>(glfwGetWindowUserPointer(window));
    display->on_iconify(window, iconified);
  });
}

OpenGLDisplay::~OpenGLDisplay() {
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  glfwSetKeyCallback(m_window, NULL);
  glfwSetWindowPosCallback(m_window, NULL);
  glfwSetWindowSizeCallback(m_window, NULL);
  glfwSetWindowIconifyCallback(m_window, NULL);
  glfwSetWindowUserPointer(m_window, nullptr);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(m_window);
  if (m_main) {
    gl_exit();
  }
}

void OpenGLDisplay::update_cursor_visibility(GLFWwindow* window, bool is_visible) {
  g_cursor_input_mode = (is_visible) ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED;
  glfwSetInputMode(window, GLFW_CURSOR, g_cursor_input_mode);
}

void OpenGLDisplay::on_key(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
  if (action == GlfwKeyAction::Press) {
    // lg::debug("KEY PRESS:   key: {} scancode: {} mods: {:X}", key, scancode, mods);
    Peripherals::OnKeyPress(key);
  } else if (action == GlfwKeyAction::Release) {
    // lg::debug("KEY RELEASE: key: {} scancode: {} mods: {:X}", key, scancode, mods);
    Peripherals::OnKeyRelease(key);
    OpenGLDisplay* display = reinterpret_cast<OpenGLDisplay*>(glfwGetWindowUserPointer(window));
    if (display != NULL) {  // toggle ImGui when pressing Alt
      if ((key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT) &&
          glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
        display->set_imgui_visible(!display->is_imgui_visible());
        update_cursor_visibility(window, display->is_imgui_visible());
      }
    }
  }
}

void OpenGLDisplay::on_mouse_key(GLFWwindow* window, int button, int action, int mode) {
  int key =
      button + GLFW_KEY_LAST;  // Mouse button index are appended after initial GLFW keys in newpad

  if (button == GLFW_MOUSE_BUTTON_LEFT &&
      g_cursor_input_mode ==
          GLFW_CURSOR_NORMAL) {  // Are there any other mouse buttons we don't want to use?
    Peripherals::ClearKey(key);
    return;
  }

  if (action == GlfwKeyAction::Press) {
    Peripherals::OnKeyPress(key);
  } else if (action == GlfwKeyAction::Release) {
    Peripherals::OnKeyRelease(key);
  }
}

void OpenGLDisplay::on_cursor_position(GLFWwindow* window, double xposition, double yposition) {
  Peripherals::MappingInfo mapping_info = Graphics::get_button_mapping();
  if (g_cursor_input_mode == GLFW_CURSOR_NORMAL) {
    if (is_cursor_position_valid == true) {
      Peripherals::ClearAnalogAxisValue(mapping_info, GlfwKeyCustomAxis::CURSOR_X_AXIS);
      Peripherals::ClearAnalogAxisValue(mapping_info, GlfwKeyCustomAxis::CURSOR_Y_AXIS);
      is_cursor_position_valid = false;
    }
    return;
  }

  if (is_cursor_position_valid == false) {
    last_cursor_x_position = xposition;
    last_cursor_y_position = yposition;
    is_cursor_position_valid = true;
    return;
  }

  double xoffset = xposition - last_cursor_x_position;
  double yoffset = yposition - last_cursor_y_position;

  Peripherals::SetAnalogAxisValue(mapping_info, GlfwKeyCustomAxis::CURSOR_X_AXIS, xoffset);
  Peripherals::SetAnalogAxisValue(mapping_info, GlfwKeyCustomAxis::CURSOR_Y_AXIS, yoffset);

  last_cursor_x_position = xposition;
  last_cursor_y_position = yposition;
}

void OpenGLDisplay::on_window_pos(GLFWwindow* /*window*/, int xpos, int ypos) {
  if (m_fullscreen_target_mode == GraphicsDisplayMode::Windowed) {
    m_last_windowed_xpos = xpos;
    m_last_windowed_ypos = ypos;
  }
}

void OpenGLDisplay::on_window_size(GLFWwindow* /*window*/, int width, int height) {
  if (m_fullscreen_target_mode == GraphicsDisplayMode::Windowed) {
    m_last_windowed_width = width;
    m_last_windowed_height = height;
  }
}

void OpenGLDisplay::on_iconify(GLFWwindow* window, int iconified) {
  m_minimized = iconified == GLFW_TRUE;
}

namespace {
std::string make_output_file_name(const std::string& file_name) {
  file_util::create_dir_if_needed(file_util::get_file_path({"Graphics_dumps"}));
  return file_util::get_file_path({"Graphics_dumps", file_name});
}
}  // namespace

static bool endsWith(std::string_view str, std::string_view suffix) {
  return str.size() >= suffix.size() &&
         0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

void render_game_frame(int game_width,
                       int game_height,
                       int window_fb_width,
                       int window_fb_height,
                       int draw_region_width,
                       int draw_region_height,
                       int msaa_samples,
                       bool windows_borderless_hack) {
  // wait for a copied chain.
  bool got_chain = false;
  {
    std::unique_lock<std::mutex> lock(g_graphics_data->dma_mutex);
    // note: there's a timeout here. If the engine is messed up and not sending us frames,
    // we still want to run the glfw loop.
    got_chain = g_graphics_data->dma_cv.wait_for(lock, std::chrono::milliseconds(50),
                                            [=] { return g_graphics_data->has_data_to_render; });
  }
  // render that chain.
  if (got_chain) {
    g_graphics_data->frame_idx_of_input_data = g_graphics_data->frame_idx;
    RenderOptions options;
    options.game_res_w = game_width;
    options.game_res_h = game_height;
    options.window_framebuffer_width = window_fb_width;
    options.window_framebuffer_height = window_fb_height;
    options.draw_region_width = draw_region_width;
    options.draw_region_height = draw_region_height;
    options.msaa_samples = msaa_samples;
    options.draw_render_debug_window = g_graphics_data->debug_gui.should_draw_render_debug();
    options.draw_profiler_window = g_graphics_data->debug_gui.should_draw_profiler();
    options.draw_subtitle_editor_window = g_graphics_data->debug_gui.should_draw_subtitle_editor();
    options.save_screenshot = false;
    options.gpu_sync = g_graphics_data->debug_gui.should_gl_finish();
    options.borderless_windows_hacks = windows_borderless_hack;
    if (g_graphics_data->debug_gui.get_screenshot_flag()) {
      options.save_screenshot = true;
      options.game_res_w = g_graphics_data->debug_gui.screenshot_width;
      options.game_res_h = g_graphics_data->debug_gui.screenshot_height;
      options.draw_region_width = options.game_res_w;
      options.draw_region_height = options.game_res_h;
      options.msaa_samples = g_graphics_data->debug_gui.screenshot_samples;
    }
    options.draw_small_profiler_window = g_graphics_data->debug_gui.small_profiler;
    options.pmode_alp_register = g_graphics_data->pmode_alp;

    GLint msaa_max;
    glGetIntegerv(GL_MAX_SAMPLES, &msaa_max);
    if (options.msaa_samples > msaa_max) {
      options.msaa_samples = msaa_max;
    }

    if (options.save_screenshot) {
      // ensure the screenshot has an extension
      std::string temp_path = g_graphics_data->debug_gui.screenshot_name();
      if (!endsWith(temp_path, ".png")) {
        temp_path += ".png";
      }
      options.screenshot_path = make_output_file_name(temp_path);
    }
    if constexpr (run_dma_copy) {
      auto& chain = g_graphics_data->dma_copier.get_last_result();
      g_graphics_data->ogl_renderer.render(DmaFollower(chain.data.data(), chain.start_offset), options);
    } else {
      g_graphics_data->ogl_renderer.render(DmaFollower(g_graphics_data->dma_copier.get_last_input_data(),
                                                  g_graphics_data->dma_copier.get_last_input_offset()),
                                      options);
    }
  }

  // before vsync, mark the chain as rendered.
  {
    // should be fine to remove this mutex if the game actually waits for vsync to call
    // send_chain again. but let's be safe for now.
    std::unique_lock<std::mutex> lock(g_graphics_data->dma_mutex);
    g_graphics_data->engine_timer.start();
    g_graphics_data->has_data_to_render = false;
    g_graphics_data->sync_cv.notify_all();
  }
}

void OpenGLDisplay::get_position(int* x, int* y) {
  glfwGetWindowPos(m_window, x, y);
}

void OpenGLDisplay::get_size(int* width, int* height) {
  glfwGetFramebufferSize(m_window, width, height);
}

void OpenGLDisplay::get_scale(float* xs, float* ys) {
  glfwGetWindowContentScale(m_window, xs, ys);
}

void OpenGLDisplay::set_size(int width, int height) {
  glfwSetWindowSize(m_window, width, height);

  if (windowed()) {
    m_last_windowed_width = width;
    m_last_windowed_height = height;
  }
}

void OpenGLDisplay::update_fullscreen(GraphicsDisplayMode mode, int screen) {
  GLFWmonitor* monitor = get_monitor(screen);

  switch (mode) {
    case GraphicsDisplayMode::Windowed: {
      // windowed
      int x, y, width, height;

      if (m_last_fullscreen_mode == GraphicsDisplayMode::Windowed) {
        // windowed -> windowed, keep position and size
        width = m_last_windowed_width;
        height = m_last_windowed_height;
        x = m_last_windowed_xpos;
        y = m_last_windowed_ypos;
      } else {
        // fullscreen -> windowed, use last windowed size but on the monitor previously
        // fullscreened
        int monitorX, monitorY, monitorWidth, monitorHeight;
        glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorWidth, &monitorHeight);

        width = m_last_windowed_width;
        height = m_last_windowed_height;
        x = monitorX + (monitorWidth / 2) - (width / 2);
        y = monitorY + (monitorHeight / 2) - (height / 2);
      }

      glfwSetWindowAttrib(m_window, GLFW_DECORATED, GLFW_TRUE);
      glfwSetWindowFocusCallback(m_window, NULL);
      glfwSetWindowAttrib(m_window, GLFW_FLOATING, GLFW_FALSE);

      glfwSetWindowMonitor(m_window, NULL, x, y, width, height, GLFW_DONT_CARE);

      // these might have changed
      m_last_windowed_width = width;
      m_last_windowed_height = height;
      m_last_windowed_xpos = x;
      m_last_windowed_ypos = y;
    } break;
    case GraphicsDisplayMode::Fullscreen: {
      // fullscreen
      const GLFWvidmode* vmode = glfwGetVideoMode(monitor);
      glfwSetWindowAttrib(m_window, GLFW_DECORATED, GLFW_FALSE);
      glfwSetWindowFocusCallback(m_window, NULL);
      glfwSetWindowAttrib(m_window, GLFW_FLOATING, GLFW_FALSE);
      glfwSetWindowMonitor(m_window, monitor, 0, 0, vmode->width, vmode->height, GLFW_DONT_CARE);
    } break;
    case GraphicsDisplayMode::Borderless: {
      // borderless fullscreen
      int x, y;
      glfwGetMonitorPos(monitor, &x, &y);
      const GLFWvidmode* vmode = glfwGetVideoMode(monitor);
      glfwSetWindowAttrib(m_window, GLFW_DECORATED, GLFW_FALSE);
      // glfwSetWindowAttrib(m_window, GLFW_FLOATING, GLFW_TRUE);
      // glfwSetWindowFocusCallback(m_window, FocusCallback);
#ifdef _WIN32
      glfwSetWindowMonitor(m_window, NULL, x, y, vmode->width, vmode->height + 1, GLFW_DONT_CARE);
#else
      glfwSetWindowMonitor(m_window, NULL, x, y, vmode->width, vmode->height, GLFW_DONT_CARE);
#endif
    } break;
  }
}

int OpenGLDisplay::get_screen_vmode_count() {
  int count = 0;
  glfwGetVideoModes(get_monitor(fullscreen_screen()), &count);
  return count;
}

void OpenGLDisplay::get_screen_size(int vmode_idx, s32* w_out, s32* h_out) {
  GLFWmonitor* monitor = get_monitor(fullscreen_screen());
  auto vmode = glfwGetVideoMode(monitor);
  int count = 0;
  auto vmodes = glfwGetVideoModes(monitor, &count);
  if (vmode_idx >= 0) {
    vmode = &vmodes[vmode_idx];
  } else if (fullscreen_mode() == GraphicsDisplayMode::Fullscreen) {
    for (int i = 0; i < count; ++i) {
      if (!vmode || vmode->height < vmodes[i].height) {
        vmode = &vmodes[i];
      }
    }
  }
  if (w_out) {
    *w_out = vmode->width;
  }
  if (h_out) {
    *h_out = vmode->height;
  }
}

int OpenGLDisplay::get_screen_rate(int vmode_idx) {
  GLFWmonitor* monitor = get_monitor(fullscreen_screen());
  auto vmode = glfwGetVideoMode(monitor);
  int count = 0;
  auto vmodes = glfwGetVideoModes(monitor, &count);
  if (vmode_idx >= 0) {
    vmode = &vmodes[vmode_idx];
  } else if (fullscreen_mode() == GraphicsDisplayMode::Fullscreen) {
    for (int i = 0; i < count; ++i) {
      if (!vmode || vmode->refreshRate < vmodes[i].refreshRate) {
        vmode = &vmodes[i];
      }
    }
  }
  return vmode->refreshRate;
}

GLFWmonitor* OpenGLDisplay::get_monitor(int index) {
  if (index < 0 || index >= g_glfw_state.monitor_count) {
    // out of bounds, default to primary monitor
    index = 0;
  }

  return g_glfw_state.monitors[index];
}

int OpenGLDisplay::get_monitor_count() {
  return g_glfw_state.monitor_count;
}

bool OpenGLDisplay::minimized() {
  return m_minimized;
}

void OpenGLDisplay::set_lock(bool lock) {
  glfwSetWindowAttrib(m_window, GLFW_RESIZABLE, lock ? GLFW_TRUE : GLFW_FALSE);
}

bool OpenGLDisplay::fullscreen_pending() {
  GLFWmonitor* monitor = get_monitor(fullscreen_screen());
  auto vmode = glfwGetVideoMode(monitor);

  return GraphicsDisplay::fullscreen_pending() ||
         (vmode->width != m_last_video_mode.width || vmode->height != m_last_video_mode.height ||
          vmode->refreshRate != m_last_video_mode.refreshRate);
}

void OpenGLDisplay::fullscreen_flush() {
  GraphicsDisplay::fullscreen_flush();

  GLFWmonitor* monitor = get_monitor(fullscreen_screen());
  auto vmode = glfwGetVideoMode(monitor);

  m_last_video_mode = *vmode;
}

/*!
 * Main function called to render graphics frames. This is called in a loop.
 */
void OpenGLDisplay::render() {
  // poll events
  {
    glfwPollEvents();
    glfwMakeContextCurrent(m_window);

    auto& mapping_info = Graphics::get_button_mapping();
    Peripherals::update_gamepads(mapping_info);
  }

  // imgui start of frame
  {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  // framebuffer size
  int fbuf_w, fbuf_h;
  glfwGetFramebufferSize(m_window, &fbuf_w, &fbuf_h);
  bool windows_borderless_hacks = false;
#ifdef _WIN32
  if (last_fullscreen_mode() == GraphicsDisplayMode::Borderless) {
    windows_borderless_hacks = true;
  }
#endif

  // render game!
  if (g_graphics_data->debug_gui.should_advance_frame()) {
    int game_res_w = Graphics::g_global_settings.GameResolutionWidth;
    int game_res_h = Graphics::g_global_settings.GameResolutionHeight;
    if (game_res_w <= 0 || game_res_h <= 0) {
      // if the window size reports 0, the game will ask for a 0 sized window, and nothing likes
      // that.
      game_res_w = 640;
      game_res_h = 480;
    }
    render_game_frame(game_res_w, game_res_h, fbuf_w, fbuf_h, Graphics::g_global_settings.LetterBoxedWidth,
                      Graphics::g_global_settings.LetterBoxedHeight, Graphics::g_global_settings.MSAAsamples,
                      windows_borderless_hacks);
  }

  // render debug
  if (is_imgui_visible()) {
    g_graphics_data->debug_gui.draw(g_graphics_data->dma_copier.get_last_result().stats);
  }
  {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  // actual vsync
  g_graphics_data->debug_gui.finish_frame();
  {
    glfwSwapBuffers(m_window);
  }

  // actually wait for vsync
  if (g_graphics_data->debug_gui.should_gl_finish()) {
    glFinish();
  }

  // switch vsync modes, if requested
  if (Graphics::g_global_settings.IsVsyncEnabled != Graphics::g_global_settings.IsOldVsyncEnabled) {
    Graphics::g_global_settings.IsOldVsyncEnabled = Graphics::g_global_settings.IsVsyncEnabled;
    glfwSwapInterval(Graphics::g_global_settings.IsVsyncEnabled);
  }

  // Start timing for the next frame.
  g_graphics_data->debug_gui.start_frame();

  // toggle even odd and wake up engine waiting on vsync.
  // TODO: we could play with moving this earlier, right after the final bucket renderer.
  //       it breaks the VIF-interrupt profiling though.
  {
    std::unique_lock<std::mutex> lock(g_graphics_data->sync_mutex);
    g_graphics_data->frame_idx++;
    g_graphics_data->sync_cv.notify_all();
  }

  // update fullscreen mode, if requested
  {
    update_last_fullscreen_mode();

    if (fullscreen_pending() && !minimized()) {
      fullscreen_flush();
    }
  }

  {
    // exit if display window was closed
    if (glfwWindowShouldClose(m_window)) {
      std::unique_lock<std::mutex> lock(g_graphics_data->sync_mutex);
      MasterExit = RuntimeExitStatus::EXIT;
      g_graphics_data->sync_cv.notify_all();
    }
  }
}

/*!
 * Wait for the next vsync. Returns 0 or 1 depending on if frame is even or odd.
 * Called from the game thread, on a GOAL stack.
 */
u32 gl_vsync() {
  if (!g_graphics_data) {
    return 0;
  }
  std::unique_lock<std::mutex> lock(g_graphics_data->sync_mutex);
  auto init_frame = g_graphics_data->frame_idx_of_input_data;
  g_graphics_data->sync_cv.wait(lock, [=] {
    return (MasterExit != RuntimeExitStatus::RUNNING) || g_graphics_data->frame_idx > init_frame;
  });
  return g_graphics_data->frame_idx & 1;
}

u32 gl_sync_path() {
  if (!g_graphics_data) {
    return 0;
  }
  std::unique_lock<std::mutex> lock(g_graphics_data->sync_mutex);
  g_graphics_data->last_engine_time = g_graphics_data->engine_timer.getSeconds();
  if (!g_graphics_data->has_data_to_render) {
    return 0;
  }
  g_graphics_data->sync_cv.wait(lock, [=] { return !g_graphics_data->has_data_to_render; });
  return 0;
}

/*!
 * Send DMA to the renderer.
 * Called from the game thread, on a GOAL stack.
 */
void gl_send_chain(const void* data, u32 offset) {
  if (g_graphics_data) {
    std::unique_lock<std::mutex> lock(g_graphics_data->dma_mutex);
    if (g_graphics_data->has_data_to_render) {
      lg::error(
          "Graphics::send_chain called when the graphics renderer has pending data. Was this called "
          "multiple times per frame?");
      return;
    }

    // we copy the dma data and give a copy of it to the render.
    // the copy has a few advantages:
    // - if the game code has a bug and corrupts the DMA buffer, the renderer won't see it.
    // - the copied DMA is much smaller than the entire game memory, so it can be dumped to a
    // file
    //    separate of the entire RAM.
    // - it verifies the DMA data is valid early on.
    // but it may also be pretty expensive. Both the renderer and the game wait on this to
    // complete.

    // The renderers should just operate on DMA chains, so eliminating this step in the future
    // may be easy.

    g_graphics_data->dma_copier.set_input_data(data, offset, run_dma_copy);

    g_graphics_data->has_data_to_render = true;
    g_graphics_data->dma_cv.notify_all();
  }
}

void gl_texture_upload_now(const u8* tpage, int mode, u32 s7_ptr) {
  // block
  if (g_graphics_data) {
    // just pass it to the texture pool.
    // the texture pool will take care of locking.
    // we don't want to lock here for the entire duration of the conversion.
  }
}

void gl_poll_events() {
  glfwPollEvents();
}

void gl_set_pmode_alp(float val) {
  g_graphics_data->pmode_alp = val;
}

const GraphicsRendererModule gRendererOpenGL = {
    gl_init,                // init
    gl_make_display,        // make_display
    gl_exit,                // exit
    gl_vsync,               // vsync
    gl_sync_path,           // sync_path
    gl_send_chain,          // send_chain
    gl_texture_upload_now,  // texture_upload_now
    gl_poll_events,         // poll_events
    gl_set_pmode_alp,       // set_pmode_alp
    GraphicsPipeline::OpenGL,    // pipeline
    "OpenGL 4.3"            // name
};
