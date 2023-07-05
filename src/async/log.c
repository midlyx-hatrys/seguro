#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

log_level_t log_level = LEVEL_info;
size_t log_payload_width = 100;

scope_t *log_scope;

static size_t indentation;

static const char *level_str(log_level_t level) {
  switch (level) {
    case LEVEL_fatal: return "!!!";
    case LEVEL_error: return "!";
    case LEVEL_warning: return "?";
    case LEVEL_info: return ":";
    case LEVEL_debug: return "_";
    case LEVEL_trace: return ".";
  }
  // can't happen
  abort();
}

static const char *pretty_fname(const char *fname) {
  if (!strncmp(fname, "src/", 4))
    return &fname[4];

  size_t len = strlen(fname);
  size_t i;
  for (i = len - 1; i != 0; i--) {
    if (fname[i] == '/' && len - i > 5 && !strncmp(&fname[i + 1], "src/", 4))
      return &fname[i + 5];
  }
  return fname;
}

static size_t prefix(log_level_t level, log_context_t *ctx) {
  size_t ret = fprintf(stderr, "%s", level_str(level));
  if (ctx) {
    ret += fprintf(stderr, " ");
    ret += ctx->out(ctx, stderr);
  }
  ret += fprintf(stderr, "%*s ", (int)indentation, "");
  return ret;
}

static void suffix(size_t length,
                   const char *file, size_t line, const char *function) {
  fprintf(stderr, "%*s |%s():%s:%lu\n",
          (int)(length > log_payload_width ? 0 : log_payload_width - length),
          "", function, pretty_fname(file), line);
}

void vlog(const char *file, size_t line, const char *function,
          log_level_t level, log_context_t *ctx, const char *fmt, va_list ap) {
  if (level > log_level)
    return;

  size_t length = prefix(level, ctx);
  length += vfprintf(stderr, fmt, ap);
  suffix(length, file, line, function);

  if (level == LEVEL_fatal)
    abort();
}

void scope_enter_for_real(scope_t *scope, const char *name,
                          const char *file, size_t line, const char *function,
                          log_context_t *ctx, const char *fmt, va_list ap) {
  if (LEVEL_trace <= log_level) {
    size_t length = prefix(LEVEL_trace, ctx);
    length += fprintf(stderr, "-> %s(", name ?: function);

    if (fmt) {
      length += vfprintf(stderr, fmt, ap);
    }

    length += fprintf(stderr, ")");
    suffix(length, file, line, function);
  }

  scope->parent = log_scope;
  scope->parent_indentation = ++indentation;
  scope->name = name;
  scope->context = ctx;
  scope->file = file;
  scope->line = line;
  scope->function = function;
}

void scope_exit_for_real(scope_t *scope) {
  log_scope = scope->parent;
  indentation = scope->parent_indentation;

  if (LEVEL_trace <= log_level) {
    size_t length = prefix(LEVEL_trace, scope->context);
    length += fprintf(stderr, "<- %s()", scope->name ?: scope->function);
    suffix(length, scope->file, scope->line, scope->function);
  }
}
