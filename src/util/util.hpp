/**
 * \file
 * \brief Included by pretty much every other file.
 */

#ifndef UTIL_HPP
#define UTIL_HPP

#include <istream>
#include <ostream>
#include <utility>

/**
 * \brief The only exception type used by this app.
 *
 * Error messages are immediately printed to the log so there is no need to
 * store any data other than the error type. Any exceptions not of this type
 * come from the standard library and most likely indicate memory bugs.
 */
enum class FatalError {
  Decode,        ///< There was an error reading data from a stream.
  Encode,        ///< There was an error writing data to a stream.
  Initialize,    ///< The application failed to start.
  ResourceLimit, ///< The built-in resource limit was triggered.
  Platform       ///< A platform-specific API call failed.
};

/**
 * \brief Helper to create visitor objects with overloads.
 *
 * ```cpp
 * std::variant<int, float> v = ...;
 * std::visit(overload(
 *   [](int i) { cout << "int: " << i << endl; },
 *   [](float f) { cout << "float: " << f << endl; }), v);
 * ```
 */
template <typename... Args> auto overload(Args &&...args) {
  struct result : Args... {
    using Args::operator()...;
  };
  return result{std::forward<Args>(args)...};
}

/// A stream that writes one message to the log.
class Logger {
  static std::ostream &out;

public:
  /// Log message priority level.
  enum Level {
    Info, ///< Diagnostic messages.
    Warn, ///< Non-fatal errors.
    Crit, ///< Indicates critical systems are broken.
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
  Logger(Level lvl, std::string_view key, int line);

  Logger(const Logger &other) = delete;
  Logger &operator=(const Logger &other) = delete;
  ~Logger() { out << std::endl; }

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
  Logger(lvl, Logger::parse_file(__FILE__), __LINE__).print(__VA_ARGS__)

/// Write to the log with logger::info level.
#define log_info(...) log_dyn_level(Logger::Info, __VA_ARGS__)
/// Write to the log with logger::warn level.
#define log_warn(...) log_dyn_level(Logger::Warn, __VA_ARGS__)
/// Write to the log with logger::crit level.
#define log_crit(...) log_dyn_level(Logger::Crit, __VA_ARGS__)

/// Adapter to read from memory with C++ streams.
class ReadMemory : public std::istream {
  class StreamBuffer : public std::streambuf {
    const void *m_data;
    size_t m_size;

  public:
    StreamBuffer(const void *p, size_t n) : m_data(p), m_size(n) {
      auto base = reinterpret_cast<char *>(const_cast<void *>(m_data));
      setg(base, base, base + m_size);
    }

  protected:
    pos_type seekoff(off_type off, seekdir dir, openmode which) override {
      char *req;
      switch (dir) {
      case cur:
        req = off + gptr();
        break;
      case end:
        req = off + egptr();
        break;
      default:
        return seekpos(off, which);
      }
      if (eback() <= req && req <= egptr()) {
        setg(eback(), req, egptr());
        return req - eback();
      } else {
        return pos_type(off_type(-1));
      }
    }

    pos_type seekpos(pos_type pos, openmode which) override {
      (void)which;
      if (static_cast<size_t>(pos) <= m_size) {
        auto base = reinterpret_cast<char *>(const_cast<void *>(m_data));
        setg(base, base + pos, base + m_size);
        return pos;
      } else {
        return pos_type(off_type(-1));
      }
    }
  };

  StreamBuffer m_streambuf;

public:
  /// Create a stream that reads from main memory.
  ReadMemory(const void *p, size_t n) : m_streambuf(p, n) {
    rdbuf(&m_streambuf);
  }
};

#endif
