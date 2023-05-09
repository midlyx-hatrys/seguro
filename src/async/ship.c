#include "ship.h"

#include "cb.h"
#include "knob.h"
#include "log.h"
#include "util.h"

#include <gmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <urbit-ob.h>
#include <uv.h>


static uv_loop_t *g_loop;


#define PATP_MAX 57
#define PATP_FMT "%57s"

typedef struct db_write_op {
  enum {
    E_START,
    E_DATA,
    E_END,
  } type;
  union {
    struct {
      uint64_t id;
      size_t length;
    } header;
    struct {
      size_t h;
      size_t length;
    } data;
  } event;
} db_write_op_t;

#define READ_STATES(_)                         \
       _(START)                                 \
       _(HS_HELLO)                              \
       _(HS_POINT)                              \
       _(HS_FETCH_EID)                          \
       _(IDLE)                                  \
       _(WM_HEADER)                             \
       _(WM_DATA)                               \
       _(W_DATA)                                \
       _(R_DATA)
typedef enum {
  READ_STATES(NAME_AND_COMMA)
} read_state_t;
extern const char *read_state_str[];

// client state header, after which we put the buffers
typedef struct client {
  uintptr_t id;

  struct {
    mpz_t num;
    char patp[PATP_MAX + 1];
  } point;

  uint64_t highest_eid;

  cb_t read_buf;
  char ctl_buf[128];
  char write_buf[16 * 1024];
  cb_t db_write_ops;

  struct {
    uv_tcp_t tcp;
    struct {
      bool reading;
      bool writing;
    } open_for;
    uv_write_t write_req;
    uv_shutdown_t shutdown_req;
  } uv;

  read_state_t state, next_state;

  struct {
    union {
      struct {
        uint64_t events_left;
        uint64_t start_id, end_id;
      } batch;
      struct {
        uint64_t start_id;
        uint64_t limit;
      } read;
    } flow;
    struct {
      uint64_t id;
      size_t left;
    } event;
  } data;

  log_context_t ctx;
} client_t;

static inline __attribute__((used))
uv_handle_t *c_handle(client_t *c) {
  return (uv_handle_t *)&c->uv.tcp;
}
static inline __attribute__((used))
uv_stream_t *c_stream(client_t *c) {
  return (uv_stream_t *)&c->uv.tcp;
}

const char *read_state_str[] = {
  READ_STATES(STRING_AND_COMMA)
};

uintptr_t c_counter;

static bool c_init(client_t *c);
static void c_destroy(client_t *c);
static void c_gc(client_t *c);
static void fc_terminate(const char *file, int line, const char *function,
                         client_t *c, const char *fmt, ...)
  __attribute__ ((format (printf, 5, 6)));
#define c_terminate(c, fmt...) \
  fc_terminate(__FILE__, __LINE__, __func__, c, fmt)

static size_t c_log_out(const log_context_t *c, FILE *stream);

static void c_on_connect(client_t *c, uv_stream_t *server);
static void c_on_close(uv_handle_t *handle);
static void c_on_shutdown(uv_shutdown_t *req, int status);

static void c_on_write_ctl(uv_write_t *req, int status);
static void c_on_read_start(uv_handle_t *handle,
                            size_t suggested_size, uv_buf_t *buf);
static void c_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);

static void c_write_ctl(client_t *c, const char *fmt, ...)
  __attribute__ ((format (printf, 2, 3)));
static bool c_process_ctl(client_t *c);

static void c_fetch_eid(client_t *c);
static void c_event_start(client_t *c, uint64_t eid, uint64_t length);

static void c_enq_op(client_t *c, const db_write_op_t *op);

static void on_connect(uv_stream_t *server, int status);

#define c_fatal(c, args...) fatal(&((c)->ctx), args)
#define c_error(c, args...) error(&((c)->ctx), args)
#define c_warn(c, args...) warn(&((c)->ctx), args)
#define c_info(c, args...) info(&((c)->ctx), args)
#define c_debug(c, args...) debug(&((c)->ctx), args)
#define c_trace(c, args...) trace(&((c)->ctx), args)
#define c_scope(c, args...) scope(&((c)->ctx), args)
#define c_assert(cond, c, args...) log_assert(cond, &((c)->ctx), args)

bool c_init(client_t *c) {
  memset(c, 0, sizeof(*c));
  if (!cb_init(&c->read_buf, knob.read_buffer_size, 1, false))
    goto fail;
  if (!cb_init(&c->db_write_ops, knob.read_buffer_size / 128, sizeof(db_write_op_t), true))
    goto fail;

  c->ctx.out = c_log_out;

  c->id = c_counter++;
  return true;

 fail:
  c_destroy(c);
  return false;
}

void c_destroy(client_t *c) {
  cb_destroy(&c->db_write_ops);
  cb_destroy(&c->read_buf);
}

void c_gc(client_t *c) {
  if (c->uv.open_for.reading || c->uv.open_for.writing)
    return;
  c_destroy(c);
  free(c);
}

size_t c_log_out(const log_context_t *ctx, FILE *stream) {
  client_t *c = container_of(ctx, client_t, ctx);
  size_t ret = fprintf(stream, "%lu", c->id);
  if (c && c->point.patp[0])
    ret += fprintf(stderr, " %s", c->point.patp);
  return ret;
}

void fc_terminate(const char *file, int line, const char *function,
                  client_t *c, const char *fmt, ...) {
  if (fmt) {
    va_list args;
    va_start(args, fmt);
    vlog(file, line, function, LEVEL_error, &c->ctx, fmt, args);
    va_end(args);
  }

  if (c->uv.open_for.writing)
    uv_shutdown(&c->uv.shutdown_req, c_stream(c), c_on_shutdown);
  if (c->uv.open_for.reading)
    uv_close(c_handle(c), c_on_close);
  c_gc(c);
}

void c_on_close(uv_handle_t *handle) {
  client_t *c = container_of(handle, client_t, uv.tcp);
  c->uv.open_for.reading = false;
  c_gc(c);
}

void c_on_shutdown(uv_shutdown_t *req, int status) {
  client_t *c = container_of(req, client_t, uv.shutdown_req);
  if (status < 0)
    c_warn(c, "shutdown failed: %s", uv_strerror(status));
  c->uv.open_for.writing = false;
  c_gc(c);
}

void c_on_write_ctl(uv_write_t *req, int status) {
  client_t *c = container_of(req, client_t, uv.write_req);
  if (status < 0) {
    c_terminate(c, "ctl write failed: %s", uv_strerror(status));
    return;
  }

  c_assert(c->state != W_DATA, c, "must not be in a streaming state");
  c->state = c->next_state;
  uv_read_start(c_stream(c), c_on_read_start, c_on_read);
}

void c_write_ctl(client_t *c, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  size_t len = vsnprintf(c->write_buf, sizeof c->write_buf - 2, fmt, args);
  c_assert(len >= sizeof c->write_buf - 2, c,
           "write buffer too small for control directives");
  c->write_buf[len] = '\n';
  c->write_buf[len + 1] = '\0';
  va_end(args);

  uv_buf_t buf = uv_buf_init(c->write_buf, len + 2);
  uv_write(&c->uv.write_req, c_stream(c), &buf, 1, c_on_write_ctl);
}

bool c_process_ctl(client_t *c) {
  c_scope(c, NULL, "%s, %s", read_state_str[c->state], c->ctl_buf);
  switch (c->state) {
    case HS_HELLO: {
      int16_t proto_version;
      if (sscanf(c->ctl_buf, "HELLO %hi\n", &proto_version) != 1
          || proto_version != 0) {
        c_error(c, "expected HELLO 0");
        return false;
      }
      c->next_state = HS_POINT;
      c_write_ctl(c, "IDENTIFY 0");
      break;
    }
    case HS_POINT: {
      if (sscanf(c->ctl_buf, "POINT ~" PATP_FMT "\n", c->point.patp + 1) != 1) {
        c_error(c, "expected POINT @p");
        return false;
      }
      c->point.patp[0] = '~';
      if (!patp2num(c->point.num, c->point.patp)) {
        c_error(c, "invalid @p");
        return false;
      }
      c->next_state = HS_FETCH_EID;
      c_fetch_eid(c);
      break;
    }
    case IDLE: {
      uint64_t x, y, z;
      if (sscanf(c->ctl_buf, "SAVE MANY %lu %lu %lu\n", &x, &y, &z) == 3) {
        if (y <= c->highest_eid || y >= z) {
          c_error(c, "invalid SAVE MANY");
          return false;
        }
        c->data.flow.batch.events_left = x;
        c->data.flow.batch.start_id = y;
        c->data.flow.batch.end_id = z;
        c->next_state = WM_HEADER;
      } else if (sscanf(c->ctl_buf, "SAVE %lu %lu\n", &x, &y) == 2) {
        if (x <= c->highest_eid) {
          c_error(c, "invalid SAVE");
          return false;
        }
        c_event_start(c, x, y);
        c->next_state = W_DATA;
      } else if (sscanf(c->ctl_buf, "READ %lu %lu\n", &x, &y) == 2) {
        c->data.flow.read.start_id = x;
        c->data.flow.read.limit = y;
        c->next_state = R_DATA;
      } else {
        c_error(c, "unexpected command");
        return false;
      }
      break;
    }
    case WM_HEADER: {
      uint64_t x, y;
      if (sscanf(c->ctl_buf, "EVENT %lu %lu\n", &x, &y) != 2
          || x <= c->highest_eid) {
        c_error(c, "invalid EVENT");
        return false;
      }
      c_event_start(c, x, y);
      c->next_state = WM_DATA;
      break;
    }
    default:
      fatal(&c->ctx, "unexpected control state %s", read_state_str[c->state]);
      break;
  }

  return true;
}

void c_fetch_eid(client_t *c) {
  // TODO doit
}

void c_enq_op(client_t *c, const db_write_op_t *op) {
  db_write_op_t *where = cb_enq(&c->db_write_ops);
  *where = *op;
  // TODO trigger shit here
}

void c_event_start(client_t *c, uint64_t id, uint64_t length) {
  db_write_op_t op;
  op.type = E_START;
  op.event.header.id = id;
  op.event.header.length = length;
  c_enq_op(c, &op);
}

void c_on_read_start(uv_handle_t *handle,
                     size_t suggested_size, uv_buf_t *buf) {
  client_t *c = container_of(handle, client_t, uv.tcp);
  buf->base = cb_w_ptr(&c->read_buf);
  buf->len = cb_cw_headroom(&c->read_buf);
}

void c_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  client_t *c = container_of(stream, client_t, uv.tcp);
  c_scope(c, NULL, "%ld, %lu", nread, buf->base - (char *)c->read_buf.b);

  if (nread < 0) {
    if (nread != UV_EOF)
      c_terminate(c, "client read failed: %s", uv_strerror(nread));
    else
      c_terminate(c, NULL);
    return;
  }
  if (nread == 0) {
    // EAGAIN, EWOULDBLOCK
    return;
  }

  if (c->state == R_DATA) {
    c_terminate(c, "client is not supposed to talk now, we are streaming to it");
    return;
  }

  char *base = buf->base;
  size_t left = (size_t)nread;

  while (left) {
    c_debug(c, "state=%s, left=%lu", read_state_str[c->state], left);

    if (c->state != W_DATA && c->state != WM_DATA) {
      const size_t ctl_buf_room = sizeof c->ctl_buf - 2;
      // control content, append to buffer up to and including \n
      size_t l = strlen(c->ctl_buf);
      // leave space for '\n\0'
      size_t i = 0;
      while (l < ctl_buf_room && i < left && base[i] != '\n')
        c->ctl_buf[l++] = base[i++];

      if (l == ctl_buf_room && base[i] != '\n') {
        c_terminate(c, "command too long");
        return;
      }

      if (base[i] == '\n') {
        // consume the '\n'
        c->ctl_buf[l++] = base[i++];
        c->ctl_buf[l] = '\0';
        if (!c_process_ctl(c)) {
          c_terminate(c, NULL);
          return;
        }
        c->ctl_buf[0] = '\0';
      }

      left -= i;
      base += i;
      continue;
    }

    db_write_op_t op;
    op.type = E_DATA;
    op.event.data.h = base - buf->base;
    op.event.data.length = min(left, c->data.event.left);
    c_enq_op(c, &op);

    if (left >= op.event.data.length + 2) {
      if (base[c->data.event.left + 1] != '\n'
          || base[c->data.event.left + 2] != '\n') {
        c_terminate(c, "event %lu: bad bracketing", c->data.event.id);
        return;
      }

      if (c->state == W_DATA) {
        c->state = IDLE;
      } else {
        c_assert(c->state == WM_DATA, c,
                 "unexpected state: %s", read_state_str[c->state]);


      }

      left -= op.event.data.length + 2;
      base += op.event.data.length + 2;
      continue;
    }
  }

  if (cb_full(&c->read_buf)) {
    uv_read_stop(c_stream(c));
    return;
  }
}

void c_on_connect(client_t *c, uv_stream_t *server) {
  c_scope(c, NULL, NULL);
  uv_tcp_init(g_loop, &c->uv.tcp);
  int status = uv_accept(server, c_stream(c));
  if (status < 0) {
    c_terminate(c, "accept error: %s", uv_strerror(status));
    return;
  }

  c_debug(c, "accepted");
  c->uv.open_for.reading = c->uv.open_for.writing = true;

  c->state = START;
  c->next_state = HS_HELLO;
  c_write_ctl(c, "SEGURO 0");
}

static void on_connect(uv_stream_t *server, int status) {
  scope(NULL, NULL, "status=%d", status);
  if (status < 0) {
    error(NULL, "connection error: %s", uv_strerror(status));
    return;
  }

  client_t *c = malloc(sizeof(client_t));
  if (!c || !c_init(c)) {
    error(NULL, "alloc failure");
    free(c);
    uv_loop_close(g_loop);
    return;
  }

  c_on_connect(c, server);
}

bool ship_server_init(uv_loop_t *loop, int port) {
  g_loop = loop;

  uv_tcp_t server;
  struct sockaddr_in addr;
  uv_tcp_init(g_loop, &server);
  uv_ip4_addr("0.0.0.0", port, &addr);
  int r = uv_tcp_bind(&server, (const struct sockaddr *)&addr, 0);
  if (r) {
    error(NULL, "bind error: %s", uv_strerror(r));
    return false;
  }
  r = uv_listen((uv_stream_t *)&server, 128, on_connect);
  if (r) {
    error(NULL, "listen error: %s", uv_strerror(r));
    return false;
  }

  return true;
}
