#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

typedef struct log_context {
  size_t (*out)(const struct log_context *c, FILE *stream);
} log_context_t;

typedef enum {
  LEVEL_fatal,
  LEVEL_error,
  LEVEL_warning,
  LEVEL_info,
  LEVEL_debug,
  LEVEL_trace,
} log_level_t;

typedef struct scope {
  struct scope *parent;
  size_t parent_indentation;
  log_context_t *context;
  const char *name;
  const char *file;
  size_t line;
  const char *function;
} scope_t;

extern log_level_t log_level;
extern size_t log_payload_width;
extern scope_t *log_scope;

void vlog(const char *file, size_t line, const char *function,
          log_level_t level, log_context_t *ctx, const char *fmt, va_list ap)
  __attribute__((format(printf, 6, 0)));

static inline __attribute__((format(printf, 6, 7), used))
void flog(const char *file, size_t line, const char *function,
          log_level_t level, log_context_t *ctx, const char *fmt, ...) {
  if (level <= log_level) {
    va_list args;
    va_start(args, fmt);
    vlog(file, line, function, level, ctx, fmt, args);
    va_end(args);
  }
}

#define log(level, c, fmt...) \
  flog(__FILE__, __LINE__, __func__, level, c, fmt)

#define die(c, fmt...) log(LEVEL_fatal, c, fmt)
#define complain(c, fmt...) log(LEVEL_error, c, fmt)
#define warn(c, fmt...) log(LEVEL_warning, c, fmt)
#define inform(c, fmt...) log(LEVEL_info, c, fmt)
#define blab(c, fmt...) log(LEVEL_debug, c, fmt)
#define spew(c, fmt...) log(LEVEL_trace, c, fmt)

// like assert() but never compiled out and using our formatter
#define assert(c, cond)                                             \
  do { if (!(cond)) die(c, "assertion failed: " #cond); } while (0)

void scope_enter(scope_t *scope, const char *name,
                 const char *file, size_t line, const char *function,
                 log_context_t *ctx, const char *fmt, ...)
  __attribute__((format(printf, 7, 8)));
void scope_exit(scope_t *scope);

#define scope(c, name, fmt...)                                      \
  __attribute__((cleanup(scope_exit))) scope_t _scope;                  \
  scope_enter(&_scope, name, __FILE__, __LINE__, __func__, c, fmt);

#define g_die(fmt...) die(NULL, fmt)
#define g_complain(fmt...) complain(NULL, fmt)
#define g_warn(fmt...) warn(NULL, fmt)
#define g_inform(fmt...) inform(NULL, fmt)
#define g_blab(fmt...) blab(NULL, fmt)
#define g_spew(fmt...) spew(NULL, fmt)

#define g_assert(cond) assert(NULL, cond)

#define g_named_scope(name, fmt...) scope(NULL, name, fmt)
#define g_scope(fmt...) scope(NULL, NULL, fmt)
