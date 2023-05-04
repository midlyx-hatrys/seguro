#include "log.h"

#include "client.h"

#include <stdio.h>

log_level_t log_level = LEVEL_info;

static const char *level_str(log_level_t level) {
  switch (level) {
    case LEVEL_error: return "E";
    case LEVEL_warn: return "W";
    case LEVEL_info: return "I";
    case LEVEL_debug: return "D";
  }
}

void vlog(struct client *c, log_level_t level, const char *fmt, va_list ap) {
  if (level > log_level)
    return;
  fprintf(stderr, "[%s%s%s] ", level_str(level), c ? " " : "", c ? c->point.patp : "");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
}
