#include <getopt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <uv.h>


#define DEFAULT_MAX_TRANSACTION_SIZE 1000000
#define DEFAULT_CHUNK_SIZE 10000

int port = 7000;
size_t max_transaction_size = DEFAULT_MAX_TRANSACTION_SIZE;
size_t buffer_size;
size_t chunk_size = DEFAULT_CHUNK_SIZE;
const char *cluster_file_path = "/etc/foundationdb/fdb.cluster";

#define container_of(ptr, type, member) \
  ((type *)((uint8_t *)(ptr) - offsetof(type, member)))

typedef struct stream {
  uint64_t event_id;
  size_t left;
  size_t start, end;
} stream_t;

// client state header, after which we put the buffers
typedef struct client {
  struct {
    uint64_t id;
    size_t data_length, done_length;
    size_t start, end;
  } db_stream;
  struct {
    uv_tcp_t tcp;
    uv_write_t write;
  } uv;
  bool read_stopped;
  bool buffers_flipped;
  enum {
    // handshake
    HS_HELLO,
    HS_POINT,
    // idle
    IDLE,
    // write flow
    W_REQUEST,
    W_EVENT_HEADER,
    W_EVENT_DATA,
    W_BRACKET,
    // read flow
    R_REQUEST,
  } state;
  char parse_buffer[256];

} client_t;
client_t *make_client(void) {
  client_t *client = malloc(sizeof(client_t) + 2 *  buffer_size);
  return client;
}
void destroy_client(client_t *client) {
  free(client);
}
static inline uint8_t *db_buffer(client_t *client) {
  return (uint8_t *)client + sizeof *client + (client->buffers_flipped ? buffer_size : 0);
}
static inline uint8_t *client_read_buffer(client_t *client) {
  return (uint8_t *)client + sizeof *client + (client->buffers_flipped ? 0 : buffer_size);
}

uv_loop_t *loop;

static inline uv_handle_t *uv_handle(client_t *client) {
  return (uv_handle_t *)&client->uv.tcp; }
static inline uv_stream_t *uv_stream(client_t *client) {
  return (uv_stream_t *)&client->uv.tcp; }

void on_client_read_start(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  client_t *client = container_of(handle, client_t, uv.tcp);
  buf->base = (char *)client_read_buffer(client);
  buf->len = buffer_size;
  client->read_stopped = false;
}

void on_client_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {

}

void on_client_connection(uv_stream_t *server, int status) {
  if (status < 0) {
    fprintf(stderr, "connection error: %s\n", uv_strerror(status));
    return;
  }

  client_t *client = make_client();
  if (!client) {
    fprintf(stderr, "alloc failure\n");
    uv_loop_close(loop);
    return;
  }

  uv_tcp_init(loop, &client->uv.tcp);
  status = uv_accept(server, uv_stream(client));
  if (status < 0) {
    fprintf(stderr, "accept error: %s\n", uv_strerror(status));
    uv_close(uv_handle(client), NULL);
    destroy_client(client);
    return;
  }

  client->state = HS_HELLO;
  memset(client->parse_buffer, 0, sizeof client->parse_buffer);

  uv_read_start(uv_stream(client), on_client_read_start, on_client_read);
}

void set_buffer_size(void) {
  buffer_size = max_transaction_size / 10 * 9;
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
  set_buffer_size();

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
  r = uv_listen((uv_stream_t *)&server, 128, on_client_connection);
  if (r) {
    fprintf(stderr, "listen error: %s\n", uv_strerror(r));
    return EXIT_FAILURE;
  }

  return uv_run(loop, UV_RUN_DEFAULT);
}
