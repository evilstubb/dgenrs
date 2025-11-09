#include "video.hpp"

#include <cassert>
#include <iterator>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "gles2.h"

/******************************************************************************
 *** Shader system. ***********************************************************
 ******************************************************************************/

/// Manage GLSL shader(s) including inputs and outputs.
class shader {
  /// This class gets its own reference to OpenGL function pointers.
  GladGLES2Context &gl;

  GLuint program;     ///< The only GLSL program.
  GLint a_color;      ///< GLSL attribute location.
  GLint a_position;   ///< GLSL attribute location.
  GLint a_texcoord;   ///< GLSL attribute location.
  GLint u_flags;      ///< GLSL uniform location.
  GLint u_color;      ///< GLSL uniform location.
  GLint u_mvp_matrix; ///< GLSL uniform location.
  GLint u_tex_matrix; ///< GLSL uniform location.

public:
  explicit shader(GladGLES2Context &gl) : gl(gl) {
    GLuint vsh = make_shader(GL_VERTEX_SHADER, "vert.glsl");
    GLuint fsh = make_shader(GL_FRAGMENT_SHADER, "frag.glsl");
    program = make_program(vsh, fsh);
    gl.DeleteShader(vsh);
    gl.DeleteShader(fsh);
    a_color = gl.GetAttribLocation(program, "a_color");
    a_position = gl.GetAttribLocation(program, "a_position");
    a_texcoord = gl.GetAttribLocation(program, "a_texcoord");
    u_flags = gl.GetUniformLocation(program, "u_flags");
    u_color = gl.GetUniformLocation(program, "u_color");
    u_mvp_matrix = gl.GetUniformLocation(program, "u_mvp_matrix");
    u_tex_matrix = gl.GetUniformLocation(program, "u_tex_matrix");
    // These settings are never changed so set them here.
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.UseProgram(program);
    GLint u_texture = gl.GetUniformLocation(program, "u_texture");
    gl.Uniform1i(u_texture, 0);
    gl.VertexAttrib4fv(a_color, glm::value_ptr(glm::vec4(1)));
  }

  shader(const shader &other) = delete;
  shader &operator=(const shader &other) = delete;
  ~shader() { gl.DeleteProgram(program); }

private:
  GLuint make_program(GLuint vsh, GLuint fsh) {
    log_info("Linking GLSL program");
    GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vsh);
    gl.AttachShader(program, fsh);
    gl.LinkProgram(program);
    gl.DetachShader(program, vsh);
    gl.DetachShader(program, fsh);
    GLint log_length;
    gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    if (log_length > 1) {
      auto mem = std::make_unique<char[]>(log_length);
      gl.GetProgramInfoLog(program, log_length, nullptr, mem.get());
      GLint status;
      gl.GetProgramiv(program, GL_LINK_STATUS, &status);
      if (status == GL_TRUE)
        log_warn("%s", mem.get());
      else
        log_crit("%s", mem.get());
    }
    return program;
  }

  GLuint make_shader(GLenum kind, const char *key) {
    log_info("Compiling GLSL shader: %s", key);
    GLuint shader = gl.CreateShader(kind);
    {
      size_t num;
      std::unique_ptr<uint8_t[]> code = read_asset(num, key);
      GLint fix_num = num;
      const GLchar *fix_code = reinterpret_cast<const GLchar *>(code.get());
      gl.ShaderSource(shader, 1, &fix_code, &fix_num);
    }
    gl.CompileShader(shader);
    GLint log_length;
    gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    if (log_length > 1) {
      auto mem = std::make_unique<char[]>(log_length);
      gl.GetShaderInfoLog(shader, log_length, nullptr, mem.get());
      GLint status;
      gl.GetShaderiv(shader, GL_COMPILE_STATUS, &status);
      if (status == GL_TRUE)
        log_warn("%s", mem.get());
      else
        log_crit("%s", mem.get());
    }
    return shader;
  }

public:
  /// Configure vertex attributes using the bound GL_ARRAY_BUFFER.
  template <typename T> void bind_attributes() {
    if constexpr (T::has_color) {
      gl.EnableVertexAttribArray(a_color);
      gl.VertexAttribPointer(
          a_color, decltype(T::color)::length(), GL_FLOAT, GL_FALSE, sizeof(T),
          reinterpret_cast<const void *>(offsetof(T, color)));
    }
    if constexpr (T::has_position) {
      gl.EnableVertexAttribArray(a_position);
      gl.VertexAttribPointer(
          a_position, decltype(T::position)::length(), GL_FLOAT, GL_FALSE,
          sizeof(T), reinterpret_cast<const void *>(offsetof(T, position)));
    }
    if constexpr (T::has_texcoord) {
      gl.EnableVertexAttribArray(a_texcoord);
      gl.VertexAttribPointer(
          a_texcoord, decltype(T::texcoord)::length(), GL_FLOAT, GL_FALSE,
          sizeof(T), reinterpret_cast<const void *>(offsetof(T, texcoord)));
    }
  }

  /**
   * \brief Set shader uniforms before rendering.
   * \param texture if non-zero, enable texture mapping
   */
  void bind_uniforms(glm::vec4 color, glm::mat4 mvp, glm::mat3 tex_matrix,
                     GLuint texture) {
    gl.Uniform1i(u_flags, texture ? 0x01 : 0x00);
    gl.Uniform4fv(u_color, 1, glm::value_ptr(color));
    gl.UniformMatrix4fv(u_mvp_matrix, 1, GL_FALSE, glm::value_ptr(mvp));
    gl.UniformMatrix3fv(u_tex_matrix, 1, GL_FALSE, glm::value_ptr(tex_matrix));
  }
};

/******************************************************************************
 *** Texture system. **********************************************************
 ******************************************************************************/

/// Manage all OpenGL texture objects.
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
  /// Access the underlying OpenGL texture object.
  GLuint operator[](index i) {
    assert(i.value < MAX_TEXTURES);
    return textures[i.value];
  }

  /// Implement texture::upload().
  void upload(index i, const_image_view iv) {
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, guess_alignment(iv.stride()));
    gl.BindTexture(GL_TEXTURE_2D, operator[](i));
    GLenum format, type;
    to_opengl_format(format, type, iv.kind());
    gl.TexImage2D(GL_TEXTURE_2D, 0, format, iv.width(), iv.height(), 0, format,
                  type, iv.pixel(0, 0));
  }

  /// Implement texture::upload_part().
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

/******************************************************************************
 *** Sprite system. ***********************************************************
 ******************************************************************************/

/// Draw textured quads on the screen.
class sys_sprite {
  /// This class gets its own reference to OpenGL function pointers.
  GladGLES2Context &gl;
  /// Access the GLSL shader to control rendering inputs.
  shader &shaders;
  /// Look up textures referenced by sprites.
  texture::data &textures;

  /// All sprites will draw with this vertex buffer (unit square).
  GLuint vbo;

  /// Vertex layout used for OpenGL vertex attributes.
  struct vertex {
    glm::vec2 position;
    glm::vec2 texcoord;

    static const bool has_color = false;
    static const bool has_position = true;
    static const bool has_texcoord = true;
  };

public:
  sys_sprite(GladGLES2Context &gl, shader &shaders, texture::data &textures)
      : gl(gl), shaders(shaders), textures(textures) {
    // Write to the vertex buffer. Don't set any attributes because this OpenGL
    // version doesn't have vertex array objects (VAOs).
    gl.GenBuffers(1, &vbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    vertex vertices[] = {
        {glm::vec2(0, 0), glm::vec2(0, 1)},
        {glm::vec2(1, 0), glm::vec2(1, 1)},
        {glm::vec2(1, 1), glm::vec2(1, 0)},
        {glm::vec2(0, 1), glm::vec2(0, 0)},
    };
    gl.BufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
  }

  sys_sprite(const sys_sprite &other) = delete;
  sys_sprite &operator=(const sys_sprite &other) = delete;
  ~sys_sprite() { gl.DeleteBuffers(1, &vbo); }

  /// Implement sys_video::draw_sprite().
  void draw_sprite(sprite sprite, glm::mat4 mvp) {
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    shaders.bind_attributes<vertex>();
    shaders.bind_uniforms(
        sprite.color, mvp, sprite.texture_matrix,
        sprite.texture.is_null() ? 0 : textures[sprite.texture.value]);
    gl.DrawArrays(GL_TRIANGLE_FAN, 0, 4);
  }
};

void sprite::set_texture(::texture::index texture, glm::vec2 pmin,
                         glm::vec2 qdim) {
  this->texture = texture;
  texture_matrix = glm::scale(glm::translate(glm::mat4(1), glm::vec3(pmin, 0)),
                              glm::vec3(qdim, 1));
}

/******************************************************************************
 *** Rendering context. *******************************************************
 ******************************************************************************/

struct sys_video::data {
  GladGLES2Context gl;
  shader shaders;
  texture::data textures;
  sys_sprite sprites;

  template <typename T>
  explicit data(T &&func)
      : gl(load_functions(std::forward<T>(func))), shaders(gl), textures(gl),
        sprites(gl, shaders, textures) {}

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

void sys_video::draw_sprite(sprite sprite, glm::mat4 mvp) {
  m_data->sprites.draw_sprite(sprite, mvp);
}

texture sys_video::new_texture() { return m_data->textures.new_texture(); }

void sys_video::delete_texture(texture::index i) {
  m_data->textures.delete_texture(i);
}

texture sys_video::operator[](texture::index i) {
  return texture(m_data->textures, i);
}
