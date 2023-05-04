#pragma once

#include <stdarg.h>

typedef enum {
  LEVEL_error,
  LEVEL_warn,
  LEVEL_info,
  LEVEL_debug,
} log_level_t;

struct client;

extern log_level_t log_level;

void vlog(struct client *c, log_level_t level, const char *fmt, va_list ap)
  __attribute__((format(printf, 3, 0)));

#define LOGGER_(level)                                  \
  static void __attribute__((format(printf, 2, 3)))     \
  level(struct client *c, const char *fmt, ...) {       \
    if (LEVEL_##level <= log_level) {                   \
      va_list args;                                     \
      va_start(args, fmt);                              \
      vlog(c, LEVEL_##level, fmt, args);                \
      va_end(args);                                     \
    }                                                   \
  }

LOGGER_(error)
LOGGER_(warn)
LOGGER_(info)
LOGGER_(debug)
