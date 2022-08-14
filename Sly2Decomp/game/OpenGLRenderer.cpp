#include "OpenGLRenderer.h"

#include "common/log/log.h"
#include "common/util/FileUtil.h"
#include "common/math/Vector.h"

#include "opengl.h"

#include "imgui.h"

namespace {
std::string g_current_render;

}

/*!
 * OpenGL Error callback. If we do something invalid, this will be called.
 */
void GLAPIENTRY opengl_error_callback(GLenum source,
                                      GLenum type,
                                      GLuint id,
                                      GLenum severity,
                                      GLsizei /*length*/,
                                      const GLchar* message,
                                      const void* /*userParam*/) {
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
    lg::debug("OpenGL notification 0x{:X} S{:X} T{:X}: {}", id, source, type, message);
  } else if (severity == GL_DEBUG_SEVERITY_LOW) {
    lg::info("[{}] OpenGL message 0x{:X} S{:X} T{:X}: {}", g_current_render, id, source, type,
             message);
  } else if (severity == GL_DEBUG_SEVERITY_MEDIUM) {
    lg::warn("[{}] OpenGL warn 0x{:X} S{:X} T{:X}: {}", g_current_render, id, source, type,
             message);
  } else if (severity == GL_DEBUG_SEVERITY_HIGH) {
    lg::error("[{}] OpenGL error 0x{:X} S{:X} T{:X}: {}", g_current_render, id, source, type,
              message);
    // ASSERT(false);
  }
}

OpenGLRenderer::OpenGLRenderer() {
  // setup OpenGL errors
  glEnable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback(opengl_error_callback, nullptr);
  // disable specific errors
  const GLuint gl_error_ignores_api_other[1] = {0x20071};
  glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 1,
                        &gl_error_ignores_api_other[0], GL_FALSE);

  lg::debug("OpenGL context information: {}", (const char*)glGetString(GL_VERSION));
}

/*!
 * Main render function. This is called from the gfx loop with the chain passed from the game.
 */
void OpenGLRenderer::render(DmaFollower dma, const RenderOptions& settings) {
  {
    setup_frame(settings);
    if (settings.gpu_sync) {
      glFinish();
    }
  }

  if (settings.draw_render_debug_window) {
    //draw_renderer_selection_window();
    // add a profile bar for the imgui stuff
    if (settings.gpu_sync) {
      glFinish();
    }
  }

  m_last_pmode_alp = settings.pmode_alp_register;

  if (settings.save_screenshot) {
    Fbo* screenshot_src;

    // can't screenshot from a multisampled buffer directly -
    if (m_fbo_state.resources.resolve_buffer.valid) {
      screenshot_src = &m_fbo_state.resources.resolve_buffer;
    } else {
      screenshot_src = m_fbo_state.render_fbo;
    }
    finish_screenshot(settings.screenshot_path, screenshot_src->width, screenshot_src->height, 0, 0,
                      screenshot_src->fbo_id);
  }
  if (settings.gpu_sync) {
    glFinish();
  }
}

namespace {
Fbo make_fbo(int w, int h, int msaa, bool make_zbuf_and_stencil) {
  Fbo result;
  bool use_multisample = msaa > 1;

  // make framebuffer object
  glGenFramebuffers(1, &result.fbo_id);
  glBindFramebuffer(GL_FRAMEBUFFER, result.fbo_id);
  result.valid = true;

  // make texture that will hold the colors of the framebuffer
  GLuint tex;
  glGenTextures(1, &tex);
  result.tex_id = tex;
  glActiveTexture(GL_TEXTURE0);
  if (use_multisample) {
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, msaa, GL_RGBA8, w, h, GL_TRUE);
  } else {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  }
  // make depth and stencil buffers that will hold the... depth and stencil buffers
  if (make_zbuf_and_stencil) {
    GLuint zbuf;
    glGenRenderbuffers(1, &zbuf);
    result.zbuf_stencil_id = zbuf;
    glBindRenderbuffer(GL_RENDERBUFFER, zbuf);
    if (use_multisample) {
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa, GL_DEPTH24_STENCIL8, w, h);
    } else {
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    }
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, zbuf);
  }
  // attach texture to framebuffer as target for colors

  if (use_multisample) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, tex, 0);
  } else {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  }

  GLenum render_targets[1] = {GL_COLOR_ATTACHMENT0};
  glDrawBuffers(1, render_targets);
  auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

  if (status != GL_FRAMEBUFFER_COMPLETE) {
    lg::error("Failed to setup framebuffer: {} {} {} {}\n", w, h, msaa, make_zbuf_and_stencil);
    switch (status) {
      case GL_FRAMEBUFFER_UNDEFINED:
        printf("GL_FRAMEBUFFER_UNDEFINED\n");
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        printf("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT\n");
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        printf("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT\n");
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
        printf("GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER\n");
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
        printf("GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER\n");
        break;
      case GL_FRAMEBUFFER_UNSUPPORTED:
        printf("GL_FRAMEBUFFER_UNSUPPORTED\n");
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
        printf("GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE\n");
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
        printf("GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS\n");
        break;
    }
    ASSERT(false);
  }

  result.multisample_count = msaa;
  result.multisampled = use_multisample;
  result.is_window = false;
  result.width = w;
  result.height = h;

  return result;
}
}  // namespace

/*!
 * Pre-render frame setup.
 */
void OpenGLRenderer::setup_frame(const RenderOptions& settings) {
  // glfw controls the window framebuffer, so we just update the size:
  auto& window_fb = m_fbo_state.resources.window;
  bool window_resized = window_fb.width != settings.window_framebuffer_width ||
                        window_fb.height != settings.window_framebuffer_height;
  window_fb.valid = true;
  window_fb.is_window = true;
  window_fb.fbo_id = 0;
  window_fb.width = settings.window_framebuffer_width;
  window_fb.height = settings.window_framebuffer_height;
  window_fb.multisample_count = 1;
  window_fb.multisampled = false;

  // see if the render FBO is still applicable
  if (!m_fbo_state.render_fbo || window_resized ||
      !m_fbo_state.render_fbo->matches(settings.game_res_w, settings.game_res_h,
                                       settings.msaa_samples)) {
    // doesn't match, set up a new one for these settings
    lg::info("FBO Setup: requested {}x{}, msaa {}", settings.game_res_w, settings.game_res_h,
             settings.msaa_samples);

    // clear old framebuffers
    m_fbo_state.resources.render_buffer.clear();
    m_fbo_state.resources.resolve_buffer.clear();

    // first, see if we can just render straight to the display framebuffer.
    if (window_fb.matches(settings.game_res_w, settings.game_res_h, settings.msaa_samples)) {
      // it matches - no need for extra framebuffers.
      lg::info("FBO Setup: rendering directly to window framebuffer");
      m_fbo_state.render_fbo = &m_fbo_state.resources.window;
    } else {
      lg::info("FBO Setup: window didn't match: {} {}", window_fb.width, window_fb.height);

      // create a fbo to render to, with the desired settings
      m_fbo_state.resources.render_buffer =
          make_fbo(settings.game_res_w, settings.game_res_h, settings.msaa_samples, true);
      m_fbo_state.render_fbo = &m_fbo_state.resources.render_buffer;

      bool msaa_matches = window_fb.multisample_count == settings.msaa_samples;

      if (!msaa_matches) {
        lg::info("FBO Setup: using second temporary buffer: res: {}x{} {}x{}", window_fb.width,
                 window_fb.height, settings.game_res_w, settings.game_res_h);

        // we'll need a temporary fbo to do the msaa resolve step
        // non-multisampled, and doesn't need z/stencil
        m_fbo_state.resources.resolve_buffer =
            make_fbo(settings.game_res_w, settings.game_res_h, 1, false);
      } else {
        lg::info("FBO Setup: not using second temporary buffer");
      }
    }
  }

  ASSERT_MSG(settings.game_res_w > 0 && settings.game_res_h > 0,
             fmt::format("Bad viewport size from game_res: {}x{}\n", settings.game_res_w,
                         settings.game_res_h));

  if (!m_fbo_state.render_fbo->is_window) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_fbo_state.resources.window.width, m_fbo_state.resources.window.height);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClearDepth(0.0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glDisable(GL_BLEND);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_state.render_fbo->fbo_id);
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClearDepth(0.0);
  glClearStencil(0);
  glDepthMask(GL_TRUE);
  // Note: could rely on sky renderer to clear depth and color, but this causes problems with
  // letterboxing
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glDisable(GL_BLEND);

  // center the letterbox
  double draw_offset_x =
      (settings.window_framebuffer_width - settings.draw_region_width) / 2;
  double draw_offset_y =
      (settings.window_framebuffer_height - settings.draw_region_height) / 2;

  if (m_fbo_state.render_fbo->is_window) {
    glViewport(draw_offset_x, draw_offset_y,
        settings.draw_region_width, settings.draw_region_height);
  } else {
    glViewport(0, 0, settings.game_res_w, settings.game_res_h);
  }
}

/*!
 * Take a screenshot!
 */
void OpenGLRenderer::finish_screenshot(const std::string& output_name,
                                       int width,
                                       int height,
                                       int x,
                                       int y,
                                       GLuint fbo) {
  std::vector<u32> buffer(width * height);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  GLint oldbuf;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldbuf);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
  // flip upside down in place
  for (int h = 0; h < height / 2; h++) {
    for (int w = 0; w < width; w++) {
      std::swap(buffer[h * width + w], buffer[(height - h - 1) * width + w]);
    }
  }

  // set alpha. For some reason, image viewers do weird stuff with alpha.
  for (auto& px : buffer) {
    px |= 0xff000000;
  }
  file_util::write_rgba_png(output_name, buffer.data(), width, height);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, oldbuf);
}

