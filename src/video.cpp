#include "video.hpp"

#include <cassert>
#include <iterator>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "gles2.h"

/******************************************************************************
 *** Texture system. **********************************************************
 ******************************************************************************/

void sprite::set_texture(::texture::index texture, glm::vec2 pmin,
                         glm::vec2 qdim) {
  this->texture = texture;
  texture_matrix = glm::scale(glm::translate(glm::mat4(1), glm::vec3(pmin, 0)),
                              glm::vec3(qdim, 1));
}

class texture::data {
  /// This class gets its own reference to OpenGL function pointers.
  GladGLES2Context &gl;

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
  explicit data(GladGLES2Context &gl) : gl(gl) {
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

private:
  static unsigned guess_alignment(unsigned num) {
    if (num % 8 == 0)
      return 8;
    else if (num % 4 == 0)
      return 4;
    else if (num % 2 == 0)
      return 2;
    else
      return 1;
  }

  static void to_opengl_format(GLenum &format, GLenum &type, image_type kind) {
    const struct {
      GLenum format;
      GLenum type;
    } table[] = {
        {GL_LUMINANCE, GL_UNSIGNED_BYTE},
        {GL_RGB, GL_UNSIGNED_BYTE},
        {GL_RGBA, GL_UNSIGNED_BYTE},
    };
    auto ikind = static_cast<unsigned>(kind);
    assert(ikind < std::size(table));
    format = table[ikind].format;
    type = table[ikind].type;
  }

public:
  GLuint operator[](index i) {
    assert(i.value < MAX_TEXTURES);
    return textures[i.value];
  }

  void upload(index i, const_image_view iv) {
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, guess_alignment(iv.stride()));
    gl.BindTexture(GL_TEXTURE_2D, operator[](i));
    GLenum format, type;
    to_opengl_format(format, type, iv.kind());
    gl.TexImage2D(GL_TEXTURE_2D, 0, format, iv.width(), iv.height(), 0, format,
                  type, iv.pixel(0, 0));
  }

  void upload_part(index i, const_image_view iv, glm::uvec2 offset) {
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, guess_alignment(iv.stride()));
    gl.BindTexture(GL_TEXTURE_2D, operator[](i));
    GLenum format, type;
    to_opengl_format(format, type, iv.kind());
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, offset.x, offset.y, iv.width(),
                     iv.height(), format, type, iv.pixel(0, 0));
  }
};

void texture::upload(const_image_view iv) { m_data.upload(m_index, iv); }

void texture::upload_part(const_image_view iv, glm::uvec2 xy) {
  m_data.upload_part(m_index, iv, xy);
}

struct sys_video::data {
  GladGLES2Context gl;
  texture::data textures;

  template <typename T>
  explicit data(T &&func)
      : gl(load_functions(std::forward<T>(func))), textures(gl) {}

  data(const data &other) = delete;
  data &operator=(const data &other) = delete;

private:
  template <typename T> static GladGLES2Context load_functions(T &&func) {
    GladGLES2Context gl;
    gladLoadGLES2ContextUserPtr(
        &gl,
        [](void *userptr, const char *name) {
          T *func = static_cast<T *>(userptr);
          return func->operator()(name);
        },
        &func);
    log_info("OpenGL vendor: %s", gl.GetString(GL_VENDOR));
    log_info("OpenGL renderer: %s", gl.GetString(GL_RENDERER));
    log_info("OpenGL version: %s", gl.GetString(GL_VERSION));
    return gl;
  }
};

sys_video::sys_video(std::function<proc(const char *)> get_proc_address)
    : m_data(new data(std::move(get_proc_address))) {}

sys_video::~sys_video() {}

void sys_video::fill_screen(glm::vec4 color) {
  m_data->gl.ClearColor(color.r, color.g, color.b, color.a);
  m_data->gl.Clear(GL_COLOR_BUFFER_BIT);
}

texture sys_video::new_texture() { return m_data->textures.new_texture(); }

void sys_video::delete_texture(texture::index i) {
  m_data->textures.delete_texture(i);
}

texture sys_video::operator[](texture::index i) {
  return texture(m_data->textures, i);
}
