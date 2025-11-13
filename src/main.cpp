#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "util.hpp"
#include "video.hpp"

namespace {

/// Manage the main window and OpenGL context.
class window {
  SDL_Window *m_window;
  SDL_GLContext m_opengl;

public:
  window() {
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    m_window = SDL_CreateWindow(
        SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING), 1200,
        800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
      log_crit("SDL_CreateWindow: %s", SDL_GetError());
      throw fatal_error::initialization;
    }
    m_opengl = SDL_GL_CreateContext(m_window);
    if (!m_opengl) {
      log_crit("SDL_GL_CreateContext: %s", SDL_GetError());
      throw fatal_error::initialization;
    }
  }

  window(const window &other) = delete;
  window &operator=(const window &other) = delete;

  ~window() {
    if (!SDL_GL_DestroyContext(m_opengl))
      log_warn("SDL_GL_DestroyContext: %s", SDL_GetError());
    SDL_DestroyWindow(m_window);
  }

  operator SDL_Window *() { return m_window; }
};

class app {
  window m_window;
  sys_video m_video;
  text::index m_text;

public:
  app() : m_video(SDL_GL_GetProcAddress) {
    text t = m_video.new_text();
    text::layout layout;
    t.set_string(layout, "Hello", 16);
    m_text = t;
  }

  bool handle_event(SDL_Event event) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
      return false;
    case SDL_EVENT_WINDOW_RESIZED:
      m_video.set_viewport(0, 0, event.window.data1, event.window.data2);
      return true;
    default:
      return true;
    }
  }

  void tick() {
    m_video.fill_screen(glm::vec4(1, 0, 1, 1));
    m_video[m_text].draw(glm::vec4(1), glm::mat4(1));
    if (!SDL_GL_SwapWindow(m_window)) {
      log_crit("SDL_GL_SwapWindow: %s", SDL_GetError());
      throw fatal_error::platform;
    }
  }
};

} // namespace

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  (void)argc;
  (void)argv;
  SDL_SetLogOutputFunction(
      [](void *userdata, int category, SDL_LogPriority priority,
         const char *message) {
        (void)userdata;
        (void)category;
        switch (priority) {
        case SDL_LOG_PRIORITY_TRACE:
        case SDL_LOG_PRIORITY_VERBOSE:
        case SDL_LOG_PRIORITY_DEBUG:
        case SDL_LOG_PRIORITY_INFO:
          log_info("%s", message);
          break;
        case SDL_LOG_PRIORITY_WARN:
          log_warn("%s", message);
          break;
        default:
          log_crit("%s", message);
          break;
        }
      },
      nullptr);
  SDL_SetHint(SDL_HINT_LOGGING, "info");
  SDL_SetAppMetadata("dgenrs", nullptr, "com.evilstubb.dgenrs");
  if (SDL_Init(SDL_INIT_VIDEO)) {
    try {
      *appstate = new app;
      return SDL_APP_CONTINUE;
    } catch (...) {
      SDL_Quit();
      throw;
    }
  } else {
    log_crit("SDL_Init: %s", SDL_GetError());
    throw fatal_error::initialization;
  }
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  auto app = static_cast<class app *>(appstate);
  try {
    app->tick();
    return SDL_APP_CONTINUE;
  } catch (...) {
    delete app;
    SDL_Quit();
    throw;
  }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  auto app = static_cast<class app *>(appstate);
  try {
    if (app->handle_event(*event))
      return SDL_APP_CONTINUE;
    else
      return SDL_APP_SUCCESS;
  } catch (...) {
    delete app;
    SDL_Quit();
    throw;
  }
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)result;
  auto app = static_cast<class app *>(appstate);
  delete app;
}
