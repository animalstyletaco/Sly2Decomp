#pragma once

/*!
 * @file display.h
 * Display for graphics. This is the game window, distinct from the runtime console.
 */

#include <memory>
#include <vector>

#include "Graphics.h"

#include "common/util/Assert.h"
#include "common/common_types.h"

// a GfxDisplay class is equivalent to a window that displays stuff. This holds an actual internal
// window pointer used by whichever renderer. It also contains functions for setting and
// retrieving certain window parameters.
// Maybe this is better implemented as an abstract class and renderers would have overrides?
class GraphicsDisplay {
  const char* m_title;

  int m_fullscreen_screen = -1;
  int m_fullscreen_target_screen = -1;
  bool m_imgui_visible;

 protected:
  bool m_main;
  // next mode
  GraphicsDisplayMode m_fullscreen_target_mode = GraphicsDisplayMode::Windowed;
  // current mode (start as -1 to force an initial fullscreen update)
  GraphicsDisplayMode m_fullscreen_mode = (GraphicsDisplayMode)-1;
  // previous mode (last frame)
  GraphicsDisplayMode m_last_fullscreen_mode = GraphicsDisplayMode::Windowed;

  int m_last_windowed_xpos = 0;
  int m_last_windowed_ypos = 0;
  int m_last_windowed_width = 640;
  int m_last_windowed_height = 480;

 public:
  virtual ~GraphicsDisplay() {}

  virtual void* get_window() const = 0;
  virtual void set_size(int w, int h) = 0;
  virtual void update_fullscreen(GraphicsDisplayMode mode, int screen) = 0;
  virtual void get_scale(float* x, float* y) = 0;
  virtual int get_screen_vmode_count() = 0;
  virtual void get_screen_size(int vmode_idx, s32* w, s32* h) = 0;
  virtual int get_screen_rate(int vmode_idx) = 0;
  virtual int get_monitor_count() = 0;
  virtual void get_position(int* x, int* y) = 0;
  virtual void get_size(int* w, int* h) = 0;
  virtual void render() = 0;
  virtual void set_lock(bool lock) = 0;
  virtual bool minimized() = 0;
  virtual bool fullscreen_pending() {
    return fullscreen_mode() != m_fullscreen_target_mode ||
           m_fullscreen_screen != m_fullscreen_target_screen;
  }
  virtual void fullscreen_flush() {
    update_fullscreen(m_fullscreen_target_mode, m_fullscreen_target_screen);

    m_fullscreen_mode = m_fullscreen_target_mode;
    m_fullscreen_screen = m_fullscreen_target_screen;
  }

  bool is_active() const { return get_window() != nullptr; }
  void set_title(const char* title);
  const char* title() const { return m_title; }
  void set_fullscreen(GraphicsDisplayMode mode, int screen) {
    m_fullscreen_target_mode = mode;
    m_fullscreen_target_screen = screen;
  }
  void update_last_fullscreen_mode() { m_last_fullscreen_mode = fullscreen_mode(); }
  GraphicsDisplayMode last_fullscreen_mode() const { return m_last_fullscreen_mode; }
  GraphicsDisplayMode fullscreen_mode() { return m_fullscreen_mode; }
  int fullscreen_screen() const { return m_fullscreen_screen; }
  void set_imgui_visible(bool visible) { m_imgui_visible = visible; }
  bool is_imgui_visible() const { return m_imgui_visible; }
  bool windowed() { return fullscreen_mode() == GraphicsDisplayMode::Windowed; }

  int width();
  int height();
};

namespace Display {

// a list of displays. the first one is the "main" display, all others are spectator-like extra
// views.
extern std::vector<std::shared_ptr<GraphicsDisplay>> g_displays;

int InitMainDisplay(int width,
                    int height,
                    const char* title,
                    GraphicsSettings& settings,
                    GameVersion version);
void KillDisplay(std::shared_ptr<GraphicsDisplay> display);
void KillMainDisplay();

std::shared_ptr<GraphicsDisplay> GetMainDisplay();

}  // namespace Display
