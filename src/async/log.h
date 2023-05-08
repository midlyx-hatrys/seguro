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
  LEVEL_warn,
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
extern scope_t *log_scope;

void vlog(const char *file, size_t line, const char *function,
          log_level_t level, log_context_t *ctx, const char *fmt, va_list ap)
  __attribute__((format(printf, 6, 0)));

static inline __attribute__((format(printf, 6, 7), used))
void flog_with_level(const char *file, size_t line, const char *function,
                     log_level_t level, log_context_t *ctx, const char *fmt, ...) {
  if (level <= log_level) {
    va_list args;
    va_start(args, fmt);
    vlog(file, line, function, level, ctx, fmt, args);
    va_end(args);
  }
}

#define log_with_level(level, c, fmt...) \
  flog_with_level(__FILE__, __LINE__, __func__, level, c, fmt)

#define fatal(c, fmt...) log_with_level(LEVEL_fatal, c, fmt)
#define error(c, fmt...) log_with_level(LEVEL_error, c, fmt)
#define warn(c, fmt...) log_with_level(LEVEL_warn, c, fmt)
#define info(c, fmt...) log_with_level(LEVEL_info, c, fmt)
#define debug(c, fmt...) log_with_level(LEVEL_debug, c, fmt)
#define trace(c, fmt...) log_with_level(LEVEL_trace, c, fmt)

// like assert() but never compiled out and using our formatter
#define fatal_unless(cond, c, fmt...)          \
  do { if (!(cond)) fatal(c, fmt); } while (0)

void scope_enter(scope_t *scope, const char *name,
                 const char *file, size_t line, const char *function,
                 log_context_t *ctx, const char *fmt, ...)
  __attribute__((format(printf, 7, 8)));
void scope_exit(scope_t *scope);

#define scope(c, name, fmt...)                                          \
  __attribute__((cleanup(scope_exit))) scope_t _scope;                  \
  scope_enter(&_scope, name, __FILE__, __LINE__, __func__, c, fmt);
