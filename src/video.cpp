#include "video.hpp"

#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

#include <freetype/freetype.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <hb-ft.h>
#include <hb.h>

#include "gles2.h"

/******************************************************************************
 **** Shader system. **********************************************************
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
  /// Vertex layout used for OpenGL vertex attributes.
  struct p2t2 {
    glm::vec2 position;
    glm::vec2 texcoord;

    static const bool has_color = false;
    static const bool has_position = true;
    static const bool has_texcoord = true;
  };

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
 **** Texture system. *********************************************************
 ******************************************************************************/

/// Manage all OpenGL texture objects.
class texture::data {
  /// This class gets its own reference to OpenGL function pointers.
  GladGLES2Context &gl;

  static constexpr index::value_type MAX_TEXTURES = 128;
  /// Map texture::index to OpenGL texture objects.
  static_int_set<index::value_type, MAX_TEXTURES> map;
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
    optional_array_index<index::value_type> i = map.insert();
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
 **** Text system. ************************************************************
 ******************************************************************************/

class text::data {
  class layout_algo {
    /// Allow hb_buffer_t to be stored in std::unique_ptr.
    struct delete_shape {
      void operator()(hb_buffer_t *buf) const { hb_buffer_destroy(buf); }
    };

    /// HarfBuzz object containing the shaping outputs.
    std::unique_ptr<hb_buffer_t, delete_shape> m_buffer;
    const hb_glyph_info_t *m_glyph_info;       ///< Array of glyph identifiers.
    const hb_glyph_position_t *m_glyph_offset; ///< Array of glyph offsets.
    unsigned m_glyph_count;
    unsigned m_index; ///< Current iteration index.

    float m_clip;
    float m_wrap;
    layout m_stats;

  public:
    explicit layout_algo(hb_buffer_t *buf, float clip, float wrap)
        : m_buffer(buf), m_index(0), m_clip(clip), m_wrap(wrap) {
      m_glyph_info = hb_buffer_get_glyph_infos(buf, &m_glyph_count);
      m_glyph_offset = hb_buffer_get_glyph_positions(buf, &m_glyph_count);
    }

    /**
     * \brief
     * \param[out] cp
     * \param[out] offset
     * \return false if there are no more glyphs
     */
    bool next(uint32_t &cp, glm::uvec2 &offset) {
      if (m_glyph_count <= m_index)
        return false;
    }

    layout stats() const { return m_stats; }
  };

  /// Use FreeType and HarfBuzz to render and shape text.
  class font {
    // Most FreeType binaries don't include the error string table.
#ifndef FT_CONFIG_OPTION_ERROR_STRINGS
    static const char *FT_Error_String(FT_Error code) {
      switch (code) {
#undef FTERRORS_H_
#define FT_ERRORDEF(e, v, s)                                                   \
  case v:                                                                      \
    return s;
#define FT_ERROR_START_LIST
#define FT_ERROR_END_LIST
#include <freetype/fterrors.h>
      default:
        log_crit("Unknown FreeType error code: %d", code);
        throw fatal_error::decode;
      }
    }
#endif

    /// Create and destroy the FreeType global context.
    class ft_library {
      FT_Library value;

    public:
      ft_library() {
        FT_Error status = FT_Init_FreeType(&value);
        if (status != FT_Err_Ok)
          log_crit("FT_Init_FreeType: %s", FT_Error_String(status));
        FT_Int xyz[3];
        FT_Library_Version(value, &xyz[0], &xyz[1], &xyz[2]);
        log_info("FreeType version: %d.%d.%d", xyz[0], xyz[1], xyz[2]);
        log_info("HarfBuzz version: %s", hb_version_string());
      }

      ft_library(const ft_library &other) = delete;
      ft_library &operator=(const ft_library &other) = delete;

      ~ft_library() {
        FT_Error status = FT_Done_FreeType(value);
        if (status != FT_Err_Ok)
          log_warn("FT_Done_FreeType: %s", FT_Error_String(status));
      }

      operator FT_Library() { return value; }
    };

    /// Create and destroy the FreeType font object.
    class ft_face {
      asset_file file;
      FT_StreamRec stream;
      FT_Face face;

    public:
      explicit ft_face(FT_Library library) : file("font.ttf") {
        FT_Open_Args args;
        memset(&args, 0, sizeof args);
        memset(&stream, 0, sizeof stream);
        args.flags = FT_OPEN_STREAM;
        args.stream = &stream;
        // From what I can tell, FreeType captures the stream by reference. This
        // class is now a self-referential structure (no moving!).
        stream.size = 0x7FFFFFFF; // unknown
        stream.descriptor.pointer = &file;
        stream.read = [](FT_Stream stream, unsigned long offset,
                         unsigned char *buffer,
                         unsigned long count) -> unsigned long {
          auto is = static_cast<asset_file *>(stream->descriptor.pointer);
          if (count) {
            is->read(reinterpret_cast<char *>(buffer), count);
            return is->bad() ? 0 : is->gcount();
          } else {
            is->seekg(offset, std::ios::beg);
            return !is->good();
          }
        };
        FT_Error status = FT_Open_Face(library, &args, 0, &face);
        if (status != FT_Err_Ok) {
          log_crit("FT_Open_Face: %s", FT_Error_String(status));
          throw fatal_error::decode;
        }
      }

      ft_face(const ft_face &other) = delete;
      ft_face &operator=(const ft_face &other) = delete;

      ~ft_face() {
        FT_Error status = FT_Done_Face(face);
        if (status != FT_Err_Ok)
          log_warn("FT_Done_Face: %s", FT_Error_String(status));
      }

      operator FT_Face() { return face; }
    };

    class ft_library ft_library;
    class ft_face ft_face;
    hb_font_t *hb_font;

    static void resize_freetype(FT_Face face, unsigned pt64) {
      FT_Error status = FT_Set_Char_Size(face, 0, pt64, 0, 72);
      if (status != FT_Err_Ok) {
        log_crit("FT_Set_Char_Size: %s", FT_Error_String(status));
        throw fatal_error::decode;
      }
    }

  public:
    font() : ft_face(ft_library) {
      resize_freetype(ft_face, 64);
      hb_font = hb_ft_font_create(ft_face, nullptr);
    }

    font(const font &other) = delete;
    font &operator=(const font &other) = delete;
    ~font() { hb_font_destroy(hb_font); }

    /**
     * \brief Change the font size.
     * \param pt64 font size in 1/64 points (72 pixels per inch)
     */
    void resize(unsigned pt64) {
      resize_freetype(ft_face, pt64);
      hb_ft_font_changed(hb_font);
    }

    /**
     * \brief Get a glyph's bitmap image.
     *
     * The given function is invoked as func(glm::uvec2, const_image_view) if
     * rendering is successful. Otherwise it is invoked as func().
     *
     * \param[out] func call this function with the result
     * \param code Unicode code point
     * \return bitmap image referencing temporary FreeType memory
     */
    template <typename T> auto render_glyph(T &&func, uint32_t code) {
      FT_Face face = ft_face;
      FT_Error status = FT_Load_Glyph(face, code, FT_LOAD_RENDER);
      if (status == FT_Err_Ok) {
        return func(glm::uvec2(face->glyph->metrics.horiBearingX,
                               face->glyph->metrics.horiBearingY),
                    const_image_view(
                        image_type::luminance, face->glyph->bitmap.width,
                        face->glyph->bitmap.rows, face->glyph->bitmap.pitch,
                        face->glyph->bitmap.buffer));
      } else {
        log_warn("FT_Load_Glyph: %s", FT_Error_String(status));
        return func();
      }
    }

    layout_algo shape_text(const char *str, float clip, float wrap) {
      hb_buffer_t *buf = hb_buffer_create();
      hb_buffer_add_utf8(buf, str, -1, 0, -1);
      hb_buffer_guess_segment_properties(buf);
      hb_shape(hb_font, buf, nullptr, 0);
      return layout_algo(buf, clip, wrap);
    }
  };

  /// Store all rendered glyphs in one OpenGL texture.
  class sprite_sheet {
    texture::data &m_sys_texture;
    /// Size of the texture where all glyph images are stored.
    static const unsigned SPRITE_SHEET_DIM = 1024;
    /// All glyph images are stored in this texture.
    texture::index m_texture;

  public:
    /// Uniquely identify a glyph that was previously rendered.
    struct glyph_key {
      uint32_t ch;     ///< Unicode code point.
      unsigned height; ///< Font size in 1/64 pixels.

      bool operator==(const glyph_key &other) const {
        return ch == other.ch && height == other.height;
      }
    };

    /// Store texture coordinates and other required glyph properties.
    struct glyph_value {
      /// Intrinsic bitmap offset in pixels as reported by FreeType.
      glm::vec2 qmin;
      /// Bitmap size in pixels as reported by FreeType.
      glm::vec2 qdim;
      /// Texture coordinate quad origin in the range (0, 1).
      glm::vec2 tmin;
      /// Texture coordinate quad dimensions.
      glm::vec2 tdim;
    };

  private:
    /// Allow glyph_key to be used in hash-based containers.
    struct hash_glyph_key {
      size_t operator()(const glyph_key &key) const {
        return std::hash<decltype(key.ch)>{}(key.ch) +
               std::hash<decltype(key.height)>{}(key.height);
      }
    };

    /// Track all glyphs that have been stored in the sprite sheet.
    std::unordered_map<glyph_key, glyph_value, hash_glyph_key> m_glyph_map;

  public:
    explicit sprite_sheet(texture::data &textures) : m_sys_texture(textures) {
      texture texture = textures.new_texture();
      m_texture = texture;
      // Portable way to initialize the texture to zero.
      uint8_t px[SPRITE_SHEET_DIM][SPRITE_SHEET_DIM];
      memset(px, 0, sizeof px);
      texture.upload(const_image_view(image_type::luminance, SPRITE_SHEET_DIM,
                                      SPRITE_SHEET_DIM, sizeof px[0], &px[0]));
    }

    operator GLuint() { return m_sys_texture[m_texture]; }

    /**
     * \brief Get the stored attributes of a rendered glyph.
     * \param[out] value
     * \param key unique glyph identifier
     * \return true if the glyph was found
     */
    bool find(glyph_value &value, glyph_key key) {
      auto it = m_glyph_map.find(key);
      if (it == m_glyph_map.end()) {
        return false;
      } else {
        value = it->second;
        return true;
      }
    }

    /**
     * \brief Try to insert a new glyph into the sprite sheet.
     *
     *
     *
     * \param value[inout]
     * \param key unique glyph identifier
     * \param bitmap glyph pixel data (written to the texture)
     * \return true if the bitmap was successfully packed
     */
    bool pack(glyph_value &value, glyph_key key, const_image_view bitmap) {}
  };

  /// Store all OpenGL vertex buffers. Each string has its own.
  class vertex_store {
    /// This class gets its own reference to OpenGL function pointers.
    GladGLES2Context &gl;

    static constexpr index::value_type MAX_BUFFERS = 128;
    /// Map text::index to OpenGL vertex buffers.
    static_int_set<index::value_type, MAX_BUFFERS> map;
    /// Store references to all OpenGL vertex buffers.
    GLuint buffers[MAX_BUFFERS];
    /// Store the vertex count of each buffer for glDrawArrays().
    GLsizei extents[MAX_BUFFERS];

  public:
    vertex_store(GladGLES2Context &gl) : gl(gl) {
      gl.GenBuffers(MAX_BUFFERS, buffers);
    }

    vertex_store(const vertex_store &other) = delete;
    vertex_store &operator=(const vertex_store &other) = delete;
    ~vertex_store() { gl.DeleteBuffers(MAX_BUFFERS, buffers); }

  private:
    /**
     * \brief Simple way for calling code to access glDrawArrays() parameters.
     *
     * The vertex buffers and draw indices are stored in different arrays so
     * this wrapper structure is necessary.
     */
    struct value_type {
      GLuint buffer;
      GLsizei &count;
    };

  public:
    index new_text() {
      optional_array_index<index::value_type> i = map.insert();
      if (i.is_null()) {
        log_crit("Exceeded the maximum number of OpenGL vertex buffers");
        throw fatal_error::resource_limit;
      } else {
        return i.value;
      }
    }

    void delete_text(index i) {
      assert(i.value < MAX_BUFFERS);
      map.remove(i.value);
    }

    /// Get the vertex buffer and draw parameters for the given index.
    value_type operator[](index i) {
      assert(i.value < MAX_BUFFERS);
      return {buffers[i.value], extents[i.value]};
    }
  };

  /// This class gets its own reference to OpenGL function pointers.
  GladGLES2Context &gl;
  /// Access the GLSL shader to control rendering inputs.
  shader &shaders;
  /// Render glyphs and shape text. There is only one font.
  class font font;
  /// Store rendered glyphs in texture(s) and get texture coordinates.
  sprite_sheet glyphs;
  ///
  vertex_store vertices;
  /// Transform pixels to normalized device coordinates (scale only).
  glm::mat4 truescale;
  ///
  float scale_font_size = 1;

public:
  data(GladGLES2Context &gl, shader &shaders, texture::data &textures)
      : gl(gl), shaders(shaders), glyphs(textures), vertices(gl) {
    GLint vp[4];
    gl.GetIntegerv(GL_VIEWPORT, vp);
    set_viewport(0, 0, vp[2], vp[3]);
  }

  text new_text() { return text(*this, vertices.new_text()); }
  void delete_text(text::index i) { vertices.delete_text(i); }
  void scale_text(float c) { scale_font_size = c; }

  void draw(index i, glm::vec4 color, glm::mat4 mvp) {
    gl.BindBuffer(GL_ARRAY_BUFFER, vertices[i].buffer);
    shaders.bind_attributes<shader::p2t2>();
    shaders.bind_uniforms(color, mvp * truescale, glm::mat3(1), glyphs);
    gl.DrawArrays(GL_TRIANGLES, 0, vertices[i].count);
  }

private:
  static void decode_quad(std::vector<shader::p2t2> &vec, glm::vec2 qmin,
                          glm::vec2 qdim, glm::vec2 tmin, glm::vec2 tdim) {
    glm::vec2 qmax = qmin + qdim;
    glm::vec2 tmax = tmin + tdim;
    //
    vec.push_back({qmin, glm::vec2(tmin.x, tmax.y)}); // bottom-left
    vec.push_back({glm::vec2(qmax.x, qmin.y), tmax}); // bottom-right
    vec.push_back({qmax, glm::vec2(tmax.x, tmin.y)}); // top-right
    //
    vec.push_back({qmin, glm::vec2(tmin.x, tmax.y)}); // bottom-left
    vec.push_back({qmax, glm::vec2(tmax.x, tmin.y)}); // top-right
    vec.push_back({glm::vec2(qmin.x, qmax.y), tmin}); // top-left
  }

public:
  bool set_string(layout &result, index i, const char *str, float pt,
                  float clip, float wrap) {
    bool status = true;
    std::vector<shader::p2t2> geom; // vertices
    font.resize(pt * 64);
    // Loop through all glyphs in the string.
    uint32_t cp;       // receive HarfBuzz glyph identifier
    glm::uvec2 offset; // receive HarfBuzz glyph offset
    auto it = font.shape_text(str, clip, wrap);
    while (it.next(cp, offset)) {
      sprite_sheet::glyph_key key;
      sprite_sheet::glyph_value value;   // receive FreeType glyph offset
      key.ch = cp, key.height = pt * 64; // disambiguate fractional pixels
      // Add to the sprite sheet if this glyph is missing. Get its geometry.
      if (!glyphs.find(value, key)) {
        // Render the glyph (this also gets its offset).
        bool result = font.render_glyph(
            overload([]() { return false; },
                     [&](glm::uvec2 a, const_image_view iv) {
                       value.qmin = glm::vec2(a) / static_cast<float>(64);
                       return glyphs.pack(value, key, iv);
                     }),
            cp);
        status |= result;
        if (!result)
          continue;
      }
      // Add vertices for this glyph (two triangles, six vertices).
      decode_quad(geom, glm::vec2(offset) / static_cast<float>(64) + value.qmin,
                  value.qdim, value.tmin, value.tdim);
    }
    result = it.stats();
    // Upload the resulting vertices to the OpenGL buffer.
    vertices[i].count = geom.size();
    gl.BindBuffer(GL_ARRAY_BUFFER, vertices[i].buffer);
    gl.BufferData(GL_ARRAY_BUFFER,
                  sizeof(decltype(geom)::value_type) * geom.size(), geom.data(),
                  GL_STATIC_DRAW);
    return status;
  }

  void set_viewport(unsigned x, unsigned y, unsigned w, unsigned h) {
    (void)x;
    (void)y;
    truescale =
        glm::scale(glm::mat4(1), glm::vec3(2 / static_cast<float>(w),
                                           2 / static_cast<float>(h), 1));
  }
};

void text::draw(glm::vec4 color, glm::mat4 mvp) {
  m_data.draw(m_index, color, mvp);
}

bool text::set_string(layout &result, const char *str, float pt, float clip,
                      float wrap) {
  return m_data.set_string(result, m_index, str, pt, clip, wrap);
}

/******************************************************************************
 **** Sprite system. **********************************************************
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

public:
  sys_sprite(GladGLES2Context &gl, shader &shaders, texture::data &textures)
      : gl(gl), shaders(shaders), textures(textures) {
    // Write to the vertex buffer. Don't set any attributes because this OpenGL
    // version doesn't have vertex array objects (VAOs).
    gl.GenBuffers(1, &vbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    shader::p2t2 vertices[] = {
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
    shaders.bind_attributes<shader::p2t2>();
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
 **** Rendering context. ******************************************************
 ******************************************************************************/

struct sys_video::data {
  GladGLES2Context gl;
  shader shaders;
  texture::data textures;
  sys_sprite sprites;
  text::data texts;

  template <typename T>
  explicit data(T &&func)
      : gl(load_functions(std::forward<T>(func))), shaders(gl), textures(gl),
        sprites(gl, shaders, textures), texts(gl, shaders, textures) {}

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

void sys_video::set_viewport(unsigned x, unsigned y, unsigned w, unsigned h) {
  m_data->gl.Viewport(x, y, w, h);
  m_data->texts.set_viewport(x, y, w, h);
}

void sys_video::draw_sprite(sprite sprite, glm::mat4 mvp) {
  m_data->sprites.draw_sprite(sprite, mvp);
}

text sys_video::new_text() { return m_data->texts.new_text(); }
void sys_video::delete_text(text::index i) { m_data->texts.delete_text(i); }
text sys_video::operator[](text::index i) { return text(m_data->texts, i); }
void sys_video::scale_text(float c) { m_data->texts.scale_text(c); }
texture sys_video::new_texture() { return m_data->textures.new_texture(); }

void sys_video::delete_texture(texture::index i) {
  m_data->textures.delete_texture(i);
}

texture sys_video::operator[](texture::index i) {
  return texture(m_data->textures, i);
}
