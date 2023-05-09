#include "knob.h"
#include "log.h"
#include "ship.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>


#define DEFAULT_MAX_TRANSACTION_SIZE 1000000
#define DEFAULT_CHUNK_SIZE 10000
#define DEFAULT_PORT 7000

const char *cluster_file_path = "/etc/foundationdb/fdb.cluster";

knobs_t knob = {
  .max_transaction_size = DEFAULT_MAX_TRANSACTION_SIZE,
  .max_chunk_size = DEFAULT_CHUNK_SIZE,
  .read_buffer_size = 0
};

int main(int argc, char *argv[]) {
  int port = DEFAULT_PORT;

  int opt;
  while ((opt = getopt(argc, argv, "p:t:c:f:")) != -1) {
    switch (opt) {
      case 'p':
        port = atoi(optarg);
        break;
      case 'f':
        cluster_file_path = strdup(optarg);
        break;
      case 'c':
        knob.max_chunk_size = strtoul(optarg, NULL, 0);
        break;
      case 't':
        knob.max_transaction_size = strtoul(optarg, NULL, 0);
        break;
    }
  }
  knob.read_buffer_size = knob.max_transaction_size * 2;

  uv_loop_t *loop = uv_default_loop();
  if (!ship_server_init(loop, port))
    return EXIT_FAILURE;

  return uv_run(loop, UV_RUN_DEFAULT);
}
