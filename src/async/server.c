#include "buffer.h"

#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <uv.h>


#define PATP_MAX 56
#define PATP_FMT "%56s"

#define DEFAULT_MAX_TRANSACTION_SIZE 1000000
#define DEFAULT_CHUNK_SIZE 10000

int port = 7000;
size_t max_transaction_size = DEFAULT_MAX_TRANSACTION_SIZE;
size_t buffer_size;
size_t chunk_size = DEFAULT_CHUNK_SIZE;
const char *cluster_file_path = "/etc/foundationdb/fdb.cluster";

#define container_of(ptr, type, member) \
  ((type *)((uint8_t *)(ptr) - offsetof(type, member)))
#define STRINGIFY(x) #x

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
      size_t h, t;
    } data;
  } event;
} db_write_op_t;

// client state header, after which we put the buffers
typedef struct client {
  char patp[PATP_MAX + 1];

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

  enum {
    START,
    // handshake
    HS_HELLO,
    HS_POINT,
    HS_FETCH_EID,
    // idle
    IDLE,
    // save many
    WM_HEADER,
    WM_DATA,
    // save
    W_DATA,
    // read flow
    R_DATA,
  } state, next_state;

  union {
    struct {
      uint64_t n_events;
      uint64_t start_id, end_id;
    } save_many;
    struct {
      uint64_t start_id;
      uint64_t limit;
    } read;
  } state_data;
} client_t;

static inline uv_handle_t *c_handle(client_t *client) {
  return (uv_handle_t *)&client->uv.tcp;
}
static inline uv_stream_t *c_stream(client_t *client) {
  return (uv_stream_t *)&client->uv.tcp;
}

static bool c_init(client_t *c);
static void c_destroy(client_t *c);
static void c_destroy_and_free_if_possible(client_t *c);
static void c_terminate(client_t *c);

static void c_on_close(uv_handle_t *handle);
static void c_on_shutdown(uv_shutdown_t *req, int status);

static void c_on_write(uv_write_t *req, int status);
static void c_on_read_start(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
static void c_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
static void c_on_connect(uv_stream_t *server, int status);

static __attribute__ ((format (printf, 2, 3)))
void c_write_ctl(client_t *c, const char *fmt, ...);
static bool c_process_ctl(client_t *c);

static void c_fetch_eid(client_t *c);
static void c_event_start(client_t *c, uint64_t eid, uint64_t length);

static uv_loop_t *loop;

bool c_init(client_t *c) {
  memset(c, 0, sizeof(*c));
  if (!cb_init(&c->read_buf, buffer_size, 1, false))
    goto fail;
  if (!cb_init(&c->db_write_ops, 128, sizeof(db_write_op_t), true))
    goto fail;

  return true;

 fail:
  c_destroy(c);
  return false;
}

void c_destroy(client_t *c) {
  cb_destroy(&c->db_write_ops);
  cb_destroy(&c->read_buf);
}

void c_destroy_and_free_if_possible(client_t *c) {
  if (c->uv.open_for.reading || c->uv.open_for.writing)
    return;
  c_destroy(c);
  free(c);
}

void c_terminate(client_t *c) {
  if (c->uv.open_for.writing)
    uv_shutdown(&c->uv.shutdown_req, c_stream(c), c_on_shutdown);
  if (c->uv.open_for.reading)
    uv_close(c_handle(c), c_on_close);
  c_destroy_and_free_if_possible(c);
}

void c_on_close(uv_handle_t *handle) {
  client_t *c = container_of(handle, client_t, uv.tcp);
  c->uv.open_for.reading = false;
  c_destroy_and_free_if_possible(c);
}

void c_on_shutdown(uv_shutdown_t *req, int status) {
  if (status < 0)
    fprintf(stderr, "shutdown failed: %s\n", uv_strerror(status));
  client_t *c = container_of(req, client_t, uv.shutdown_req);
  c->uv.open_for.writing = false;
  c_destroy_and_free_if_possible(c);
}

void c_on_write(uv_write_t *req, int status) {
  client_t *c = container_of(req, client_t, uv.write_req);
  if (status < 0) {
    fprintf(stderr, "write failed: %s\n", uv_strerror(status));
    c_terminate(c);
    return;
  }

  if (c->state != W_DATA) {
    c->state = c->next_state;
    uv_read_start(c_stream(c), c_on_read_start, c_on_read);
  }
}

void c_write_ctl(client_t *c, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(c->write_buf, sizeof c->write_buf, fmt, args);
  va_end(args);

  uv_buf_t buf = uv_buf_init(c->write_buf, strlen(c->write_buf));
  uv_write(&c->uv.write_req, c_stream(c), &buf, 1, c_on_write);
}

bool c_process_ctl(client_t *c) {
  switch (c->state) {
    case HS_HELLO: {
      int16_t proto_version;
      if (sscanf(c->ctl_buf, "HELLO %hi\n", &proto_version) != 1
          || proto_version != 0) {
        fprintf(stderr, "expected HELLO 0\n");
        return false;
      }
      c->next_state = HS_POINT;
      c_write_ctl(c, "IDENTIFY 0\n");
      break;
    }
    case HS_POINT: {
      if (sscanf(c->ctl_buf, "POINT ~" PATP_FMT "\n", c->patp) != 1) {
        fprintf(stderr, "expected POINT @p\n");
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
          fprintf(stderr, "invalid SAVE MANY\n");
          return false;
        }
        c->state_data.save_many.n_events = x;
        c->state_data.save_many.start_id = y;
        c->state_data.save_many.end_id = z;
        c->next_state = WM_HEADER;
      } else if (sscanf(c->ctl_buf, "SAVE %lu %lu\n", &x, &y) == 2) {
        if (x <= c->highest_eid) {
          fprintf(stderr, "invalid SAVE\n");
          return false;
        }
        c_event_start(c, x, y);
        c->next_state = W_DATA;
      } else if (sscanf(c->ctl_buf, "READ %lu %lu\n", &x, &y) == 2) {
        c->state_data.read.start_id = x;
        c->state_data.read.limit = y;
        c->next_state = R_DATA;
      } else {
        fprintf(stderr, "unexpected command\n");
        return false;
      }
      break;
    }
    case WM_HEADER: {
      uint64_t x, y;
      if (sscanf(c->ctl_buf, "EVENT %lu %lu\n", &x, &y) != 2
          || x <= c->highest_eid) {
        fprintf(stderr, "invalid EVENT\n");
        return false;
      }
      c_event_start(c, x, y);
      c->next_state = WM_DATA;
      break;
    }
    default:
      fprintf(stderr, "unexpected control state %d\n", c->state);
      abort();
      break;
  }

  return true;
}

void c_fetch_eid(client_t *c) {
  // TODO doit
}

void c_event_start(client_t *c, uint64_t eid, uint64_t length) {
  // TODO doit
}

void c_on_read_start(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  client_t *c = container_of(handle, client_t, uv.tcp);
  buf->base = cb_w_ptr(&c->read_buf);
  buf->len = cb_cw_headroom(&c->read_buf);
}

void c_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  client_t *c = container_of(stream, client_t, uv.tcp);

  if (nread < 0) {
    if (nread != UV_EOF) {
      fprintf(stderr, "client read failed: %s\n", uv_strerror(nread));
    }
    c_terminate(c);
    return;
  }

  char *base = buf->base;
  size_t left = (size_t)nread;

  while (left) {
    if (c->state != W_DATA && c->state != R_DATA) {
      // control content, append to buffer up to \n
      size_t l = strlen(c->ctl_buf);
      const size_t ctl_buf_room = sizeof c->ctl_buf - 1;
      size_t i = 0;
      while (l < ctl_buf_room && i < left && base[i] != '\n')
        c->ctl_buf[l++] = base[i++];

      if (l == ctl_buf_room) {
        fprintf(stderr, "command too long\n");
        c_terminate(c);
        return;
      }

      c->ctl_buf[l] = '\0';
      if (base[i] == '\n' && !c_process_ctl(c)) {
        c_terminate(c);
        return;
      }

      left -= i;
      base += i;
      continue;
    }

    // TODO W_DATA, deal with it
  }

  if (cb_full(&c->read_buf)) {
    uv_read_stop(c_stream(c));
    return;
  }
}

void c_on_connect(uv_stream_t *server, int status) {
  if (status < 0) {
    fprintf(stderr, "connection error: %s\n", uv_strerror(status));
    return;
  }

  client_t *c = malloc(sizeof(client_t));
  if (!c || !c_init(c)) {
    fprintf(stderr, "alloc failure\n");
    free(c);
    uv_loop_close(loop);
    return;
  }

  uv_tcp_init(loop, &c->uv.tcp);
  status = uv_accept(server, c_stream(c));
  if (status < 0) {
    fprintf(stderr, "accept error: %s\n", uv_strerror(status));
    c_terminate(c);
    return;
  }

  c->uv.open_for.reading = c->uv.open_for.writing = true;

  c->state = START;
  c->next_state = HS_HELLO;
  c_write_ctl(c, "SEGURO 0\n");
}

int main(int argc, char *argv[]) {
  int opt;
  while ((opt = getopt(argc, argv, "p:t:c:f:")) != -1) {
    switch (opt) {
      case 'p':
        port = atoi(optarg);
        break;
      case 'c':
        chunk_size = strtoul(optarg, NULL, 0);
        break;
      case 't':
        max_transaction_size = strtoul(optarg, NULL, 0);
        break;
      case 'f':
        cluster_file_path = strdup(optarg);
        break;
    }
  }
  buffer_size = max_transaction_size * 2;

  loop = uv_default_loop();
  uv_tcp_t server;
  struct sockaddr_in addr;
  uv_tcp_init(loop, &server);
  uv_ip4_addr("0.0.0.0", port, &addr);
  int r = uv_tcp_bind(&server, (const struct sockaddr *)&addr, 0);
  if (r) {
    fprintf(stderr, "bind error: %s\n", uv_strerror(r));
    return EXIT_FAILURE;
  }
  r = uv_listen((uv_stream_t *)&server, 128, c_on_connect);
  if (r) {
    fprintf(stderr, "listen error: %s\n", uv_strerror(r));
    return EXIT_FAILURE;
  }

  return uv_run(loop, UV_RUN_DEFAULT);
}
