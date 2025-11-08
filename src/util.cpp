#include "util.hpp"

#include <cassert>
#include <cstdarg>
#include <iostream>

#include <SDL3/SDL_iostream.h>

/******************************************************************************
 *** Simple logging system. ***************************************************
 ******************************************************************************/

std::ostream &logger::out = std::cerr;

logger::logger(level lvl, std::string_view key, int line) {
  constexpr std::string_view levels[] = {"INFO\0", "WARN\0", "CRIT\0"};
  auto i = static_cast<std::underlying_type_t<level>>(lvl);
  assert(i < std::size(levels));
  print("%s|%s:%d:", levels[i].data(), key.data(), line);
}

void logger::print(const char *fmt, ...) {
  // Need some glue code to use SDL's printf with C++ streams.
  constexpr auto init = []() {
    SDL_IOStreamInterface iface;
    SDL_INIT_INTERFACE(&iface);
    iface.write = [](void *userdata, const void *ptr, size_t size,
                     SDL_IOStatus *status) -> size_t {
      (void)userdata;
      out.write(static_cast<const char *>(ptr), size);
      if (out) {
        return size;
      } else {
        *status = SDL_IO_STATUS_ERROR;
        return 0;
      }
    };
    return SDL_OpenIO(&iface, nullptr);
  };
  // It will be initialized only when this function first runs.
  static SDL_IOStream *ios = init();
  va_list args;
  va_start(args, fmt);
  SDL_IOvprintf(ios, fmt, args);
  va_end(args);
}
