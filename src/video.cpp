#include "video.hpp"

#include <cassert>

#include <SDL3/SDL_video.h>

#include "gles2.h"

namespace {

/// Load OpenGL function pointers during initialization.
struct opengl_functions : GladGLES2Context {
  opengl_functions() {
    gladLoadGLES2Context(this, SDL_GL_GetProcAddress);
    log_info("OpenGL vendor: %s", GetString(GL_VENDOR));
    log_info("OpenGL renderer: %s", GetString(GL_RENDERER));
    log_info("OpenGL version: %s", GetString(GL_VERSION));
  }
};

} // namespace

class texture::data {
  /// This class gets its own reference to OpenGL function pointers.
  opengl_functions &gl;

  static constexpr index::value_type MAX_TEXTURES = 128;
  /// Map texture::index to OpenGL texture objects.
  static_int_map<index::value_type, MAX_TEXTURES> map;
  /// Store all OpenGL texture objects.
  GLuint textures[MAX_TEXTURES];

public:
  /**
   * \brief Allocate all OpenGL textures during initialization.
   *
   * The reference to OpenGL function pointers must remain valid for the
   * lifetime of this instance.
   *
   * \param gl OpenGL function pointers
   */
  explicit data(opengl_functions &gl) : gl(gl) {
    gl.GenTextures(MAX_TEXTURES, textures);
    for (GLuint texture : textures) {
      gl.BindTexture(GL_TEXTURE_2D, texture);
      // On some implementations texturing doesn't work (black screen) unless
      // some initial parameters are set, especially the min/mag filter.
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
  }

  data(const data &other) = delete;
  data &operator=(const data &other) = delete;

  /// Destroy all OpenGL texture objects.
  ~data() { gl.DeleteTextures(MAX_TEXTURES, textures); }

  /// Used by other video subsystems to create OpenGL textures.
  texture new_texture() {
    auto i = map.insert();
    if (i.is_null()) {
      log_crit("Exceeded the maximum number of OpenGL textures");
      throw fatal_error::resource_limit;
    } else {
      return texture(*this, i.value);
    }
  }

  /// Used by other video subsystems to destroy OpenGL textures.
  void delete_texture(index i) {
    assert(i.value < MAX_TEXTURES);
    map.remove(i.value);
  }

  /// Map texture::index to OpenGL texture objects.
  GLuint operator[](index i) {
    assert(i.value < MAX_TEXTURES);
    return textures[i.value];
  }
};

struct sys_video::data {
  opengl_functions gl;
  texture::data textures;

  data() : textures(gl) {}
  data(const data &other) = delete;
  data &operator=(const data &other) = delete;
};

sys_video::sys_video() : m_data(new data) {}
sys_video::~sys_video() {}
texture sys_video::new_texture() { return m_data->textures.new_texture(); }

void sys_video::delete_texture(texture::index i) {
  m_data->textures.delete_texture(i);
}

texture sys_video::operator[](texture::index i) {
  return texture(m_data->textures, i);
}
