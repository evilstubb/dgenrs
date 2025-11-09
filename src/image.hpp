/**
 * \file
 * \brief Manipulate images in main memory.
 */

#ifndef IMAGE_HPP
#define IMAGE_HPP

#include <cassert>
#include <cstdint>
#include <istream>
#include <iterator>
#include <memory>
#include <ostream>
#include <utility>

/// List of supported pixel formats.
enum class image_type {
  luminance, ///< One color channel of 8 bits.
  rgb,       ///< Three color channels with 24 bits per pixel.
  rgba,      ///< Three colors plus alpha with 32 bits per pixel.
  num_image_types
};

/// Get pixel size in bytes for the given pixel format.
constexpr unsigned bytes_per_pixel(image_type kind) {
  const unsigned table[] = {1, 3, 4};
  auto ikind = static_cast<unsigned>(kind);
  assert(ikind < std::size(table));
  return table[ikind];
}

/**
 * \brief A reference to an image. See image_view and const_image_view.
 * \tparam T pixel buffer type
 */
template <typename T> class basic_image_view {
protected:
  image_type m_kind;
  unsigned m_width;
  unsigned m_height;
  unsigned m_stride;
  T m_pixels;

  /// Only internal code is allowed to create uninitialized objects.
  basic_image_view() = default;

public:
  /**
   * \brief Reference an existing pixel buffer.
   * \param w column count
   * \param h row count
   * \param stride see stride()
   * \param pixels args to construct the pixel buffer
   */
  template <typename U>
  basic_image_view(image_type kind, unsigned w, unsigned h, unsigned stride,
                   U &&pixels)
      : m_kind(kind), m_width(w), m_height(h), m_stride(stride),
        m_pixels(std::forward<U>(pixels)) {}

  // Needed to access the members of other specializations.
  template <typename U> friend class basic_image_view;
  /// Allow conversion between image view types.
  template <typename U>
  basic_image_view(const basic_image_view<U> &other)
      : m_kind(other.m_kind), m_width(other.m_width), m_height(other.m_height),
        m_stride(other.m_stride), m_pixels(other.m_pixels) {}

  /// Get the format of the pixel buffer.
  image_type kind() const { return m_kind; }
  /// Get the number of columns of pixels.
  unsigned width() const { return m_width; }
  /// Get the number of rows of pixels.
  unsigned height() const { return m_height; }
  /// Get the number of bytes between the start of each row in memory.
  unsigned stride() const { return m_stride; }

  /// Get a pointer to the pixel at (x, y).
  typename T::pixel_type *pixel(unsigned x, unsigned y) const {
    assert(x < m_width && y < m_height);
    return m_pixels.get() + y * m_stride + x * bytes_per_pixel(m_kind);
  }

  /// Write this image to the given PNG file.
  void write_png(std::ostream &os) const;
};

/// Pixel buffer that allocates on the heap.
class pixel_storage {
  std::unique_ptr<uint8_t[]> m_pixels;

public:
  using rw_pixel_type = void;

  /**
   * \brief Type of pixel returned by basic_image_view::pixel().
   *
   * Use const pixels because image wants different const rules than
   * basic_image_view, from which it inherits.
   */
  using pixel_type = const rw_pixel_type;

  /// Forward argument(s) to the unique_ptr constructor.
  template <typename... T>
  pixel_storage(T &&...pixels) : m_pixels(std::forward<T>(pixels)...) {}

  uint8_t *get() const { return m_pixels.get(); }
};

/// Non-owning reference to an allocated pixel buffer.
class pixel_reference {
public:
  using pixel_type = void;

private:
  pixel_type *pixels;

public:
  pixel_reference() = default;
  pixel_reference(pixel_type *p) : pixels(p) {}
  uint8_t *get() const { return static_cast<uint8_t *>(pixels); }
};

/// Non-owning reference to an immutable pixel buffer.
class const_pixel_reference {
public:
  using pixel_type = const void;

private:
  pixel_type *pixels;

public:
  const_pixel_reference() = default;
  const_pixel_reference(pixel_type *p) : pixels(p) {}
  /// Allow construction of this class from an image reference.
  const_pixel_reference(const pixel_storage &p) : pixels(p.get()) {}
  const uint8_t *get() const { return static_cast<const uint8_t *>(pixels); }
};

/// A reference to an image.
using image_view = basic_image_view<pixel_reference>;
/// A reference to an immutable image.
using const_image_view = basic_image_view<const_pixel_reference>;

/// Implement basic_image_view::write_png().
void write_png(std::ostream &os, const_image_view iv);

// All specializations will call the global ::write_png().
template <typename T>
void basic_image_view<T>::write_png(std::ostream &os) const {
  ::write_png(os, *this);
}

/// An image that allocates its own pixel buffer.
class image : public basic_image_view<pixel_storage> {
  image() = default;

public:
  /// Read a PNG file from the given stream.
  static image read_png(std::istream &is);

  /// Create an image and allocate its pixel buffer.
  image(image_type kind, unsigned width, unsigned height);

  /* The const variant of pixel() is provided by basic_image_view. See
   * pixel_storage::pixel_type. */
  using basic_image_view<pixel_storage>::pixel;

  /// Get a pointer to the pixel at (x, y).
  pixel_storage::rw_pixel_type *pixel(unsigned x, unsigned y) {
    auto p = basic_image_view<pixel_storage>::pixel(x, y);
    return const_cast<void *>(p);
  }
};

#endif
