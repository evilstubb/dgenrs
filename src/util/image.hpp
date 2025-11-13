/**
 * \file
 * \brief Manipulate images in main memory.
 */

#ifndef IMAGE_HPP
#define IMAGE_HPP

#include <cassert>
#include <cstdint>
#include <istream>
#include <memory>
#include <ostream>
#include <utility>

/// List of supported pixel formats.
enum class ImageType {
  Luminance, ///< One color channel of 8 bits.
  RGB,       ///< Three color channels with 24 bits per pixel.
  RGBA,      ///< Three colors plus alpha with 32 bits per pixel.
};

/// Get pixel size in bytes for the given pixel format.
unsigned bytes_per_pixel(ImageType kind);

namespace detail::image {

/**
 * \internal
 * \brief Pixel buffer that allocates its own memory.
 */
class AllocatePixels {
  std::unique_ptr<uint8_t[]> m_data;

public:
  template <typename... T>
  AllocatePixels(T &&...args) : m_data(std::forward<T>(args)...) {}

  using PixelType = const uint8_t;
  PixelType *get() const { return m_data.get(); }
  using PixelTypeRW = uint8_t;
  PixelTypeRW *get() { return m_data.get(); }
};

/**
 * \internal
 * \brief Pixel buffer that references (but doesn't allocate) memory.
 */
class PixelReference {
public:
  using PixelType = uint8_t;

private:
  PixelType *m_data;

public:
  PixelReference(AllocatePixels &px) : m_data(px.get()) {}
  PixelType *get() const { return m_data; }
};

/**
 * \internal
 * \brief Pixel buffer that references (but doesn't allocate) memory.
 */
class ConstPixelReference {
public:
  using PixelType = const uint8_t;

private:
  PixelType *m_data;

public:
  ConstPixelReference(const AllocatePixels &px) : m_data(px.get()) {}
  ConstPixelReference(const PixelReference &px) : m_data(px.get()) {}
  PixelType *get() const { return m_data; }
};

} // namespace detail::image

/// A reference to an image in main memory.
template <typename T> class BasicImageView {
protected:
  ImageType m_kind;
  unsigned m_width;
  unsigned m_height;
  unsigned m_stride;
  T m_pixels;

public:
  BasicImageView() = default;

  template <typename... U>
  BasicImageView(ImageType kind, unsigned w, unsigned h, unsigned stride,
                 U &&...px)
      : m_kind(kind), m_width(w), m_height(h), m_stride(stride),
        m_pixels(std::forward<U>(px)...) {}

  /// Allow conversion between certain image view types.
  template <typename U> operator BasicImageView<U>() const {
    return BasicImageView<U>(m_kind, m_width, m_height, m_stride, m_pixels);
  }

  /// Allow conversion between certain image view types.
  template <typename U> operator BasicImageView<U>() {
    return BasicImageView<U>(m_kind, m_width, m_height, m_stride, m_pixels);
  }

  /// Get the pixel buffer format.
  ImageType kind() const { return m_kind; }
  /// Get the number of columns of pixels.
  unsigned width() const { return m_width; }
  /// Get the number of rows of pixels.
  unsigned height() const { return m_height; }
  /// Get the number of bytes between the start of each row in memory.
  unsigned stride() const { return m_stride; }

  /// Get a pointer to the pixel at (x, y).
  typename T::PixelType *pixel(unsigned x, unsigned y) const {
    assert(x < m_width && y < m_height);
    return m_pixels.get() + y * m_stride + x * bytes_per_pixel(m_kind);
  }

  /// Write this image to the given PNG file.
  void write_png(std::ostream &os) const;
};

/// A mutable reference to an image in main memory.
using ImageView = BasicImageView<detail::image::PixelReference>;

/**
 * \brief An immutable reference to an image in main memory.
 *
 * Instances of Image and ImageView can be converted to ConstImageView.
 */
using ConstImageView = BasicImageView<detail::image::ConstPixelReference>;

namespace detail::image {

/**
 * \internal
 * \brief Non-template implementation of BasicImageView::write_png().
 */
void write_png(std::ostream &os, ConstImageView iv);

} // namespace detail::image

// All BasicImageView specializations call detail::image::write_png().
template <typename T>
void BasicImageView<T>::write_png(std::ostream &os) const {
  // All views are convertible to ConstImageView.
  detail::image::write_png(os, *this);
}

/// An image that owns its pixel buffer memory.
class Image : public BasicImageView<detail::image::AllocatePixels> {
public:
  /// Read a PNG file from the given stream.
  static Image read_png(std::istream &is);

  Image() = default;
  /// Create a new image and allocate its pixel buffer.
  Image(ImageType kind, unsigned w, unsigned h);

  /**
   * \brief Get a pointer to the pixel at (x, y).
   *
   * This variant of the pixel() function is not const. Image has different
   * const rules than ImageView and ConstImageView.
   */
  uint8_t *pixel(unsigned x, unsigned y) {
    return const_cast<uint8_t *>(BasicImageView::pixel(x, y));
  }
};

#endif
