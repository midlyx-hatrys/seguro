#include <getopt.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <uv.h>

#define MAX_TRANSACTION_SIZE 10000000
#define DEFAULT_CHUNK_SIZE 10000

static size_t chunk_size = DEFAULT_CHUNK_SIZE;
static const char *cluster_file_path = "/etc/foundationdb/fdb.cluster";

typedef struct stream_event {
  uint64_t id;
  size_t total_size, start_size;
  uint8_t *data;
  size_t start, end;
} stream_event_t;

typedef struct stream_write_pipe {
  uint8_t *buffers[2]; // each buffer must be of MAX_TRANSACTION_SIZE
  int buffers_in_use;
  stream_event_t read, write;
} stream_write_pipe_t;

static void stream_write_pipe_init(stream_write_pipe_t *pipe) {
  memset(pipe, 0, sizeof(*pipe));
  pipe->buffers[0] = malloc(MAX_TRANSACTION_SIZE);
  pipe->buffers[1] = malloc(MAX_TRANSACTION_SIZE);
}

static void stream_write_pipe_destroy(stream_write_pipe_t *pipe) {
  free(pipe->buffers[0]);
  free(pipe->buffers[1]);
}

int main(int argc, char *argv[]) {
  int opt;
  while ((opt = getopt(argc, argv, "c:f:")) != -1) {
    switch (opt) {
      case 'c':
        chunk_size = atoi(optarg);
        break;
      case 'f':
        cluster_file_path = strdup(optarg);
        break;
    }
  }

  return EXIT_SUCCESS;
}
