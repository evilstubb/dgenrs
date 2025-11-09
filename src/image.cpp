#include "image.hpp"

#include <exception>
#include <optional>
#include <utility>

#include <png.h>

#include "util.hpp"

namespace {

/// Handle fatal libpng errors. longjmp() instead of returning.
void libpng_on_error(png_structp png, png_const_charp msg) {
  log_crit("%s", msg);
  png_longjmp(png, 1);
}

/// Handle non-fatal libpng errors (warnings).
void libpng_on_warning(png_structp png, png_const_charp msg) {
  (void)png;
  log_warn("%s", msg);
}

/// Convert image_type enum to libpng bit depth and color type.
void native_to_libpng(int &bit_depth, int &color_type, image_type kind) {
  const struct {
    int bit_depth;
    int color_type;
  } table[] = {
      {8, PNG_COLOR_TYPE_GRAY},
      {8, PNG_COLOR_TYPE_RGB},
      {8, PNG_COLOR_TYPE_RGBA},
  };
  auto ikind = static_cast<unsigned>(kind);
  assert(ikind < std::size(table));
  bit_depth = table[ikind].bit_depth;
  color_type = table[ikind].color_type;
}

} // namespace

void write_png(std::ostream &os, const_image_view iv) {
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
                                            libpng_on_error, // longjmp()
                                            libpng_on_warning);
  png_infop info = png_create_info_struct(png);
  // libpng will jump to this block without unwinding the stack if there's an
  // error. Just don't allocate anything in this function.
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    throw fatal_error::encode;
  }
  int bit_depth, color_type;
  native_to_libpng(bit_depth, color_type, iv.kind());
  png_set_IHDR(png, info, iv.width(), iv.height(), bit_depth, color_type,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_set_write_fn(
      png, &os,
      [](png_structp png, png_bytep src, png_size_t n) {
        auto os = static_cast<std::ostream *>(png_get_io_ptr(png));
        os->write(reinterpret_cast<const char *>(src), n);
      },
      nullptr);
  png_write_info(png, info);
  for (unsigned y = 0; y < iv.height(); y++)
    png_write_row(png, reinterpret_cast<png_const_bytep>(iv.pixel(0, y)));
  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);
}

namespace {

/// Convert libpng bit depth and color type to image_type enum.
image_type libpng_to_native(png_structp png, png_infop info) {
  int bit_depth = png_get_bit_depth(png, info);
  switch (bit_depth) {
  case 8:
    break;
  default:
    log_crit("Unsupported PNG bit depth: %d", bit_depth);
    throw fatal_error::decode;
  }
  int color_type = png_get_color_type(png, info);
  switch (color_type) {
  case PNG_COLOR_TYPE_GRAY:
    return image_type::luminance;
  case PNG_COLOR_TYPE_RGB:
    return image_type::rgb;
  case PNG_COLOR_TYPE_RGBA:
    return image_type::rgba;
  default:
    log_crit("Unsupported PNG color type: %d", color_type);
    throw fatal_error::decode;
  }
}

} // namespace

image image::read_png(std::istream &is) {
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
                                           libpng_on_error, // longjmp()
                                           libpng_on_warning);
  png_infop info = png_create_info_struct(png);
  // libpng will jump to this block without unwinding the stack if there's an
  // error. The pixel buffer is always freed.
  image dst;
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, NULL);
    throw fatal_error::decode;
  }
  png_set_read_fn(png, &is, [](png_structp png, png_bytep dst, size_t n) {
    auto is = static_cast<std::istream *>(png_get_io_ptr(png));
    is->read(reinterpret_cast<char *>(dst), n);
  });
  png_read_info(png, info);
  // Use transformations to convert most unsupported images into supported
  // ones. Then update the info structure to reflect the result.
  png_set_expand_gray_1_2_4_to_8(png);
  png_set_palette_to_rgb(png);
  png_set_scale_16(png);
  int passes = png_set_interlace_handling(png);
  png_read_update_info(png, info);
  // Read all image data from libpng into main memory.
  dst = image(libpng_to_native(png, info), png_get_image_width(png, info),
              png_get_image_height(png, info));
  for (int pass = 0; pass < passes; pass++)
    for (unsigned y = 0; y < dst.height(); y++)
      png_read_row(png, reinterpret_cast<png_bytep>(dst.pixel(0, y)), nullptr);
  png_read_end(png, info);
  png_destroy_read_struct(&png, &info, nullptr);
  return dst;
}

image::image(image_type kind, unsigned w, unsigned h) {
  m_kind = kind;
  m_width = w;
  m_height = h;
  m_stride = w * bytes_per_pixel(kind);
  unsigned remain = m_stride % 4; // row alignment
  m_stride = remain ? m_stride - remain + 4 : m_stride;
  m_pixels = std::make_unique<uint8_t[]>(h * m_stride);
}
