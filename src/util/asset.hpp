/**
 * \file
 * \brief Manage asset files and search paths.
 */

#ifndef ASSET_HPP
#define ASSET_HPP

#include <cstdint>
#include <istream>
#include <memory>

/**
 * \brief Read an entire file into memory.
 *
 * The resulting array is always null-terminated.
 *
 * \param[out] num file size
 * \param is file stream
 * \throw FatalError::Decode if the file can't be read
 */
std::unique_ptr<uint8_t[]> read_stream(size_t &num, std::istream &is);

/// Manage asset files and search paths.
class AssetSystem {
  class Data;
  std::unique_ptr<Data> m_data;

public:
  AssetSystem();
  AssetSystem(AssetSystem &&other);
  AssetSystem &operator=(AssetSystem &&other);

  /**
   * \brief Destroy the AssetSystem state.
   *
   * All outstanding asset streams are invalidated and can't be used.
   */
  ~AssetSystem();

  /**
   * \brief Add a folder on disk to the asset search path.
   * \param p priority (lower is high priority)
   * \param path directory path
   * \throw FatalError::Decode if the directory can't be read
   */
  void add_directory(unsigned p, const char *path);

  /**
   * \brief Add a zip file to the asset search path.
   * \param p priority (lower is high priority)
   * \param path zip file path
   * \throw FatalError::Decode if the zip file can't be read
   */
  void add_zip(unsigned p, const char *path);

  /**
   * \brief Add a zip file to the asset search path.
   *
   * The given stream reference must remain valid at least until the AssetSystem
   * is destroyed.
   *
   * \param p priority (lower is high priority)
   * \param is an open zip file
   * \throw FatalError::Decode if the zip file can't be read
   */
  void add_zip(unsigned p, std::istream &is);

  /**
   * \brief Open an asset file for reading.
   * \param key asset file name
   * \throw FatalError::Decode if the file can't be read
   */
  std::unique_ptr<std::istream> open(const char *key);
};

#endif
