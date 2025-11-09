/**
 * \file
 * \brief Included by pretty much every other file.
 */

#ifndef UTIL_HPP
#define UTIL_HPP

#include <cassert>
#include <cstdint>
#include <istream>
#include <limits>
#include <memory>
#include <ostream>
#include <string_view>
#include <type_traits>
#include <utility>

/**
 * \brief The only exception type used by this app.
 *
 * Error messages are immediately printed to the log so there is no need to
 * store any data other than the error type. Any exceptions not of this type
 * come from the standard library and most likely indicate memory bugs.
 */
enum class fatal_error {
  decode,         ///< There was an error reading data from a stream.
  encode,         ///< There was an error writing data to a stream.
  initialization, ///< The application failed to start.
  resource_limit
};

/**
 * \brief A base class for type-safe array indices.
 *
 * Inherit from this class like so:
 *
 * ```cpp
 * struct my_index : safe_array_index<unsigned> {
 *   // Also inherit base class constructors.
 *   using safe_array_index<unsigned>::safe_array_index;
 * };
 * ```
 *
 * \tparam T array index type (integer)
 */
template <typename T> struct safe_array_index {
  using value_type = T;
  static_assert(std::is_integral_v<value_type>);

  /// The array index stored in this class.
  value_type value;
  safe_array_index() {}
  safe_array_index(value_type v) : value(v) {}
};

/**
 * \brief A base class for optional array indices.
 * \tparam T array index type (integer)
 * \tparam null invalid array index which may be used as null
 */
template <typename T, T null = std::numeric_limits<T>::max()>
struct optional_array_index {
  using value_type = T;
  static_assert(std::is_integral_v<value_type>);

  /// The array index stored in this class.
  value_type value;
  /// Default initialization produces the null index.
  optional_array_index() : value(null) {}
  /// Automatic conversion from value_type to optional_array_index.
  optional_array_index(value_type v) : value(v) {}
  /// Automatic conversion from safe_array_index to optional_array_index.
  optional_array_index(safe_array_index<value_type> i) : value(i.value) {}
  /// Check if this index has the null value.
  bool is_null() const { return value == null; }
};

/**
 * \brief Use an array to map indices to values.
 *
 * The values are not contained in this data structure; it is only used to get
 * the keys (unused array indices). Because it uses an array capacity is fixed.
 *
 * \tparam T array index type (integer)
 * \tparam num array capacity (maximum index value)
 */
template <typename T, T num> class static_int_map {
public:
  using value_type = T;
  static_assert(std::is_integral_v<value_type>);

  /// Maximum number of items in the map.
  static constexpr value_type capacity = num;

private:
  value_type num_free_objects = 0;
  value_type num_used_objects = 0;
  value_type free_objects[capacity];

public:
  /// Get an unused array index. Returns the null index if there are none.
  optional_array_index<value_type> insert() {
    if (num_free_objects) {
      num_free_objects -= 1;
      return free_objects[num_free_objects];
    } else if (num_used_objects < capacity) {
      value_type i = num_used_objects;
      num_used_objects += 1;
      return i;
    } else {
      return optional_array_index<value_type>{};
    }
  }

  /// Dispose of an array index previously obtained from insert().
  void remove(value_type i) {
    assert(i < num_used_objects);
    free_objects[num_free_objects] = i;
    num_free_objects += 1;
  }
};

/******************************************************************************
 *** Simple logging library. **************************************************
 ******************************************************************************/

/// A stream that writes one message to the log.
class logger {
  static std::ostream &out;

public:
  /// Log message priority level.
  enum level {
    info, ///< Diagnostic messages.
    warn, ///< Non-fatal errors.
    crit, ///< Indicates critical systems are broken.
  };

  /**
   * \brief Parse the source file path at compile time.
   * \param value the __FILE__ macro
   */
  static constexpr std::string_view parse_file(std::string_view value) {
    auto sep = value.find_last_of("/\\");
    if (sep == std::string_view::npos)
      return value;
    else
      return value.substr(sep + 1);
  }

  /**
   * \brief Begin the message. Print the header.
   * \param lvl message priority
   * \param key see parse_file()
   * \param line the __LINE__ macro
   */
  logger(level lvl, std::string_view key, int line);

  logger(const logger &other) = delete;
  logger &operator=(const logger &other) = delete;
  ~logger() { out << std::endl; }

  /**
   * \brief Print with format string (not type-safe).
   * \param fmt printf-style format string
   * \param ... additional printf arguments
   */
  void print(const char *fmt, ...);

  /// Use this class like any C++ output stream.
  template <typename T> std::ostream &operator<<(T &&rhs) {
    return out << std::forward<T>(rhs);
  }
};

/// Write one line to the log with the given level.
#define log_dyn_level(lvl, ...)                                                \
  logger(lvl, logger::parse_file(__FILE__), __LINE__).print(__VA_ARGS__)

/// Write to the log with logger::info level.
#define log_info(...) log_dyn_level(logger::info, __VA_ARGS__)
/// Write to the log with logger::warn level.
#define log_warn(...) log_dyn_level(logger::warn, __VA_ARGS__)
/// Write to the log with logger::crit level.
#define log_crit(...) log_dyn_level(logger::crit, __VA_ARGS__)

/******************************************************************************
 *** Asset files. *************************************************************
 ******************************************************************************/

/// A stream that reads from an asset file.
class asset_file : public std::istream {
  std::unique_ptr<std::streambuf> m_streambuf;

public:
  /**
   * \brief Open an asset file for reading.
   * \param key asset file name
   * \throw fatal_error::decode if the file can't be opened
   */
  explicit asset_file(const char *key);
};

/**
 * \brief Read an entire asset file into memory.
 *
 * The resulting array is always null-terminated. The extra null byte is not
 * reflected in the value written to num.
 *
 * \param[out] num total bytes read
 * \param key asset file name
 */
std::unique_ptr<uint8_t[]> read_asset(size_t &num, const char *key);

#endif
