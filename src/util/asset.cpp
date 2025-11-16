#include "asset.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <zlib.h>

#include "util.hpp"

std::unique_ptr<uint8_t[]> read_stream(size_t &num, std::istream &is) {
  is.seekg(0, std::ios::end);
  num = is.tellg();
  if (!is.good())
    throw FatalError::Decode;
  auto mem = std::make_unique<uint8_t[]>(num + 1);
  is.seekg(0, std::ios::beg);
  is.read(reinterpret_cast<char *>(mem.get()), num);
  mem[num] = 0;
  return mem;
}

class AssetSystem::Data {
  class DirectorySource {
    std::string m_path;

  public:
    explicit DirectorySource(const char *path) {
      SDL_PathInfo info;
      if (!SDL_GetPathInfo(path, &info)) {
        log_crit("SDL_GetPathInfo: %s", SDL_GetError());
        throw FatalError::Decode;
      } else if (info.type != SDL_PATHTYPE_DIRECTORY) {
        log_crit("Directory not found: %s", path);
        throw FatalError::Decode;
      }
    }

    std::unique_ptr<std::istream> open(const char *key) {
      char path[1024];
      int num = SDL_snprintf(path, sizeof path, "%s/%s", m_path.c_str(), key);
      assert(0 <= num); // internal error?
      if (sizeof path <= static_cast<unsigned>(num)) {
        log_warn("Full asset path is too long: %d bytes", num);
        return nullptr;
      } else {
        auto is = std::make_unique<std::ifstream>(path, std::ios::binary);
        return is->good() ? std::move(is) : nullptr;
      }
    }
  };

  class ZipSource {
    /**
     * \brief
     */
    std::ifstream m_disk_file;

    ///
    std::istream &m_file;

    /**
     * \brief Store the result of reading the central directory.
     *
     * Map file path (relative to zip root) to the offset of the corresponding
     * local file header, which immediately precedes the compressed data.
     */
    std::unordered_map<std::string, size_t> m_index;

  public:
    explicit ZipSource(const char *path)
        : m_disk_file(path, std::ios::binary), m_file(m_disk_file) {
      if (!m_file.good()) {
        log_crit("Can't open zip file: %s", path);
        throw FatalError::Decode;
      }
      init();
    }

    explicit ZipSource(std::istream &is) : m_file(is) {
      assert(m_file.good());
      init();
    }

  private:
    class CatStream : public std::istream {
      class StreamBuffer : public std::streambuf {
        std::istream &m_data;
        pos_type m_off_beg;
        pos_type m_off_end;
        pos_type m_off_pos;
        char m_storage[4096];

      public:
        StreamBuffer(std::istream &data, pos_type beg, pos_type end)
            : m_data(data), m_off_beg(beg), m_off_end(end), m_off_pos(beg) {}

        int_type underflow() override {
          if (m_off_end <= m_off_pos) {
            setg(nullptr, nullptr, nullptr);
            return traits_type::eof();
          }
          m_data.seekg(m_off_pos, std::ios::beg);
          m_data.read(m_storage, // don't read past local file
                      std::min(m_off_end - m_off_pos,
                               std::streamsize(sizeof(m_storage))));
          if (!m_data.gcount()) {
            setg(nullptr, nullptr, nullptr);
            return traits_type::eof();
          }
          setg(m_storage, m_storage, m_storage + m_data.gcount());
          m_off_pos = m_data.tellg();
          return m_storage[0];
        }

        pos_type seekoff(off_type off, seekdir dir, openmode which) override {
          pos_type req;
          switch (dir) {
          case cur:
            req = off + m_off_pos;
            break;
          case end:
            req = off + m_off_end;
            break;
          default:
            return seekpos(off, which);
          }
          if (m_off_beg <= req && req <= m_off_end) {
            setg(nullptr, nullptr, nullptr);
            m_off_pos = req;
            return m_off_pos - m_off_beg;
          } else {
            return pos_type(off_type(-1));
          }
        }

        pos_type seekpos(pos_type pos, openmode which) override {
          (void)which;
          pos_type off = pos + m_off_beg;
          if (off <= m_off_end) {
            setg(nullptr, nullptr, nullptr);
            m_off_pos = off;
            return pos;
          } else {
            return pos_type(off_type(-1));
          }
        }
      };

      StreamBuffer m_underlying;

    public:
      CatStream(std::istream &data, pos_type beg, pos_type end)
          : m_underlying(data, beg, end) {
        rdbuf(&m_underlying);
      }
    };

    class DeflateStream : public std::istream {
      class StreamBuffer : public std::streambuf {
        std::istream &m_data;
        pos_type m_off_beg;
        pos_type m_off_end;
        pos_type m_off_pos;
        z_stream m_zlib;
        char m_storage[4096];

      public:
        StreamBuffer(std::istream &data, pos_type beg, pos_type end)
            : m_data(data), m_off_beg(beg), m_off_end(end), m_off_pos(beg) {
          memset(&m_zlib, 0, sizeof m_zlib);
          int status = inflateInit2(&m_zlib, -MAX_WBITS); // raw deflate
          if (status != Z_OK) {
            if (m_zlib.msg)
              log_crit("inflateInit: %s", m_zlib.msg);
            else
              log_crit("inflateInit: code %d", status);
            throw FatalError::Decode;
          }
        }

        StreamBuffer(const StreamBuffer &other) = delete;
        StreamBuffer &operator=(const StreamBuffer &other) = delete;
        ~StreamBuffer() { inflateEnd(&m_zlib); }

        int_type underflow() override {
          if (m_off_end <= m_off_pos) {
            setg(nullptr, nullptr, nullptr);
            return traits_type::eof();
          }
          // Read compressed data into this temporary buffer.
          char mem[4096];
          m_data.seekg(m_off_pos, std::ios::beg);
          m_data.read(
              mem, // don't read past local file
              std::min(m_off_end - m_off_pos, std::streamsize(sizeof(mem))));
          if (!m_data.gcount()) {
            setg(nullptr, nullptr, nullptr);
            return traits_type::eof();
          }
          m_off_pos = m_data.tellg();
          //TODO
          m_zlib.next_in = reinterpret_cast<unsigned char *>(mem);
          m_zlib.avail_in = m_data.gcount();
          m_zlib.next_out = reinterpret_cast<unsigned char *>(m_storage);
          m_zlib.avail_out = sizeof(m_storage);
          int status = inflate(&m_zlib, Z_NO_FLUSH);
        }

        // pos_type seekoff(off_type off, seekdir dir, openmode which) override
        // {}

        // pos_type seekpos(pos_type pos, openmode which) override {}
      };

      StreamBuffer m_underlying;

    public:
      DeflateStream(std::istream &data, pos_type beg, pos_type end)
          : m_underlying(data, beg, end) {
        rdbuf(&m_underlying);
      }
    };

  public:
    std::unique_ptr<std::istream> open(const char *key) {
      auto it = m_index.find(key);
      if (it == m_index.end())
        return nullptr;
      //
      uint16_t n; // file name length
      uint16_t m; // extra field length
      uint16_t compression;
      uint32_t decode_size; // uncompressed
      std::streamoff base = it->second;
      m_file.seekg(base + 8, std::ios::beg);
      read(compression, m_file);
      m_file.seekg(base + 22, std::ios::beg);
      read(decode_size, m_file);
      m_file.seekg(base + 26, std::ios::beg);
      read(n, m_file);
      read(m, m_file);
      base += 30 + n + m;
      switch (compression) {
      case 0:
        return std::make_unique<CatStream>(m_file, base, base + decode_size);
      case 8:
        return std::make_unique<DeflateStream>(m_file, base,
                                               base + decode_size);
      default:
        log_crit("Unsupported compression method: %u", compression);
        throw FatalError::Decode;
      }
    }

  private:
    void init() {
      seek_end_of_central_directory(m_file);
      // Get the location of the central directory (list of all files).
      uint16_t num_records;
      uint32_t off_records; // start of central directory
      std::streamoff base = m_file.tellg();
      m_file.seekg(base + 8, std::ios::beg);
      read(num_records, m_file);
      m_file.seekg(base + 16, std::ios::beg);
      read(off_records, m_file);
      // Add each central directory record to the index.
      base = off_records;
      for (uint16_t i = 0; i < num_records; i++) {
        uint16_t n; // file name length
        uint16_t m; // extra field length
        uint16_t k; // comment length
        m_file.seekg(base + 28, std::ios::beg);
        read(n, m_file);
        read(m, m_file);
        read(k, m_file);
        // Get the address of the file's local header.
        uint32_t off_file;
        m_file.seekg(base + 42, std::ios::beg);
        read(off_file, m_file);
        // Read the variable-length file name.
        std::string name(n, '\0');
        m_file.read(name.data(), n);
        m_index.emplace(std::move(name), off_file);
        // Advance to the next central directory record.
        base += 46 + n + m + k;
      }
    }

    static void read(uint16_t &result, std::istream &is) {
      is.read(reinterpret_cast<char *>(&result), 2);
      result = SDL_Swap16LE(result);
    }

    static void read(uint32_t &result, std::istream &is) {
      is.read(reinterpret_cast<char *>(&result), 4);
      result = SDL_Swap32LE(result);
    }

    /// Move the file position to the beginning of the EOCD record.
    static void seek_end_of_central_directory(std::istream &is) {
      std::streamsize file_size;
      is.seekg(0, std::ios::end);
      file_size = is.tellg();
      if (is.good()) {
        char window[1024];
        auto getpos = file_size - sizeof(window);
        for (int i = 0; i < 64; i += 1) {
          getpos = std::max<std::streamoff>(getpos, 0);
          is.seekg(getpos, std::ios::beg);
          is.read(window, sizeof(window));
          const uint8_t sig[4] = {0x50, 0x4b, 0x05, 0x06};
          for (int j = is.gcount() - sizeof(sig); j >= 0; j -= 1) {
            if (memcmp(sig, window + j, sizeof(sig)) == 0) {
              is.clear(std::ios::eofbit);
              is.seekg(getpos + j, std::ios::beg);
              return;
            }
          }
          if (getpos)
            getpos -= sizeof(window);
          else
            break;
        }
      }
      log_crit("Can't find the EOCD record");
      throw FatalError::Decode;
    }
  };

  using AnySource = std::variant<DirectorySource, ZipSource>;

  std::multimap<unsigned, AnySource> m_search_path;

public:
  void add_directory(unsigned p, const char *path) {
    m_search_path.emplace(
        std::piecewise_construct, std::forward_as_tuple(p),
        std::forward_as_tuple(std::in_place_type<DirectorySource>, path));
  }

  template <typename T> void add_zip(unsigned p, T &&init) {
    m_search_path.emplace(
        std::piecewise_construct, std::forward_as_tuple(p),
        std::forward_as_tuple(
            // stream reference (std::istream&) or file path (const char*)
            std::in_place_type<ZipSource>, std::forward<T>(init)));
  }

  std::unique_ptr<std::istream> open(const char *key) {
    for (auto &[p, source] : m_search_path) {
      std::unique_ptr<std::istream> result =
          std::visit([=](auto &resolve) { return resolve.open(key); }, source);
      if (result) {
        assert(result->good());
        return result;
      }
    }
    log_crit("Asset file not found: %s", key);
    throw FatalError::Decode;
  }
};

AssetSystem::AssetSystem() : m_data(new Data) {}
AssetSystem::AssetSystem(AssetSystem &&other) = default;
AssetSystem &AssetSystem::operator=(AssetSystem &&other) = default;
AssetSystem::~AssetSystem() = default;

void AssetSystem::add_directory(unsigned p, const char *path) {
  m_data->add_directory(p, path);
}

void AssetSystem::add_zip(unsigned p, const char *path) {
  m_data->add_zip(p, path);
}

void AssetSystem::add_zip(unsigned p, std::istream &is) {
  m_data->add_zip(p, is);
}

std::unique_ptr<std::istream> AssetSystem::open(const char *key) {
  return m_data->open(key);
}
