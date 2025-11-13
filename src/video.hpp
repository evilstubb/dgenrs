/**
 * \file
 * \brief Low-level OpenGL rendering system.
 */

#ifndef VIDEO_HPP
#define VIDEO_HPP

#include <functional>
#include <memory>

#include <glm/glm.hpp>

#include "image.hpp"
#include "util.hpp"

/// Render text with OpenGL (and FreeType and HarfBuzz).
class text {
public:
  class data;

  /**
   * \brief A type-safe text reference that can be stored.
   *
   * It's not ideal to store instances of the text class because each has its
   * own reference to the text system data. Instead, store optional_index.
   */
  struct index : safe_array_index<unsigned> {
    using safe_array_index<unsigned>::safe_array_index;
  };

  /// A text index that hasn't been checked for null.
  struct optional_index : optional_array_index<unsigned> {
    using optional_array_index<unsigned>::optional_array_index;
  };

private:
  data &m_data;
  index m_index;

public:
  text(data &d, index i) : m_data(d), m_index(i) {}
  operator index() const { return m_index; }

  /// Text rendering feedback which may be useful for layout purposes.
  struct layout {
    unsigned lines; ///< Line count, accounting for text wrapping.
    glm::vec2 pmin; ///< Bounding box minimum point in pixels.
    glm::vec2 qdim; ///< Bounding box dimensions in pixels.
  };

  /**
   * \brief Change the rendered string and font size.
   *
   * If clipping is used, glyphs that partially intersect the clipping plane
   * won't be clipped. Calling code is responsible for pixel-accurate clipping.
   *
   * \param[out] layout
   * \param str new text to render
   * \param pt font size in points (1/72 inches)
   * \param clip optionally clip glyphs past this height in pixels
   * \param wrap optionally wrap lines at this width in pixels
   */
  bool set_string(layout &result, const char *str, float pt, float clip = 0,
                  float wrap = 0);

  /**
   * \brief Render the current string with OpenGL.
   * \param mvp model-view-projection matrix
   */
  void draw(glm::vec4 color, glm::mat4 mvp);
};

/// A reference to an allocated OpenGL texture.
class texture {
public:
  class data;

  /**
   * \brief A type-safe texture reference that can be stored.
   *
   * It's not ideal to store instances of the texture class because each has its
   * own reference to the texture system data. Instead, store optional_index.
   */
  struct index : safe_array_index<unsigned> {
    using safe_array_index<unsigned>::safe_array_index;
  };

  /// A texture index that hasn't been checked for null.
  struct optional_index : optional_array_index<unsigned> {
    using optional_array_index<unsigned>::optional_array_index;
  };

private:
  data &m_data;
  index m_index;

public:
  texture(data &d, index i) : m_data(d), m_index(i) {}
  operator index() const { return m_index; }

  /// Replace the contents of this texture.
  void upload(const_image_view iv);
  /// Replace part of this texture with the given image.
  void upload_part(const_image_view iv, glm::uvec2 xy);
};

/// Variables to draw 2D shapes on the screen.
struct sprite {
  glm::vec4 color = glm::vec4(1);    ///< Multiply all pixels by this value.
  glm::mat3 texture_matrix;          ///< Texture coordinate matrix.
  ::texture::optional_index texture; ///< Optional texture mapping.

  /**
   * \brief Set the texture and texture coordinate matrix.
   * \param texture allocated texture object
   * \param pmin texture coordinate quad minimum point (s, t)
   * \param qdim texture coordinate quad dimensions
   */
  void set_texture(::texture::index texture, glm::vec2 pmin, glm::vec2 qdim);
};

/// Low-level OpenGL rendering system.
class sys_video {
  struct data;
  std::unique_ptr<data> m_data;

public:
  /// An OpenGL function pointer.
  using proc = void (*)();

  explicit sys_video(std::function<proc(const char *)> get_proc_address);
  ~sys_video();

  /// Clear the screen with the given color.
  void fill_screen(glm::vec4 color);

  /**
   * \brief Draw the given sprite on the screen.
   * \param mvp model-view-projection matrix
   */
  void draw_sprite(sprite sprite, glm::mat4 mvp);

  /// Change the OpenGL viewport.
  void set_viewport(unsigned x, unsigned y, unsigned w, unsigned h);

  text new_text();
  void delete_text(text::index i);
  text operator[](text::index i);
  void scale_text(float c);

  /// Get an unused OpenGL texture.
  texture new_texture();
  /// Delete an OpenGL texture. It might be reused by new_texture().
  void delete_texture(texture::index i);
  /// Access an allocated OpenGL texture by its index.
  texture operator[](texture::index i);
};

#endif
