/**
 * \file
 * \brief Low-level OpenGL rendering system.
 */

#ifndef VIDEO_HPP
#define VIDEO_HPP

#include <memory>

#include "util.hpp"

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
  texture(class data &d, index i) : m_data(d), m_index(i) {}
  operator index() const { return m_index; }
};

/// Low-level OpenGL rendering system.
class sys_video {
  struct data;
  std::unique_ptr<data> m_data;

public:
  sys_video();
  ~sys_video();

  /// Get an unused OpenGL texture.
  texture new_texture();
  /// Delete an OpenGL texture. It might be reused by new_texture().
  void delete_texture(texture::index i);
  /// Access an allocated OpenGL texture by its index.
  texture operator[](texture::index i);
};

#endif
