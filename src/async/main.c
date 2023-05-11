#include "knob.h"
#include "log.h"
#include "ship.h"
#include "util.h"

#include <argtable3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>


#define DEFAULT_TX_SIZE 1000000
#define DEFAULT_CHUNK_SIZE 10000
#define DEFAULT_TX_BUFFERING 2
#define DEFAULT_PORT 7000
#define DEFAULT_CLUSTER "/etc/foundationdb/fdb.cluster"

struct arg_int *port;
struct arg_file *cluster;
struct arg_int *chunk_size, *tx_size, *btxs;
struct arg_lit *help, *verbosity;
struct arg_int *log_width;
struct arg_end *end;

knobs_t knob = {
  .tx_size = DEFAULT_TX_SIZE,
  .chunk_size = DEFAULT_CHUNK_SIZE,
  .read_buffer_size = 0
};

int main(int argc, char *argv[]) {
  void *argt[] = {
    help = arg_litn("h", "help", 0, 1, "display this help and exit"),
    verbosity = arg_litn("v", "verbose", 0, LEVEL_trace - log_level, "log verbosity"),
    log_width = arg_int0("w", "log-width", "<n>", "log message width"),
    port = arg_int0("p", "port", "<port>", "TCP port to listen on"),
    cluster = arg_file0("d", "db-cluster", "<path>", "FDB cluster file"),
    chunk_size = arg_int0("c", "chunk-size", "<n>", "max chunk size"),
    tx_size = arg_int0("t", "tx-size", "<n>", "max tx size"),
    btxs = arg_int0("b", "buffered-txs", "<n>", "how many txs to buffer up"),
    end = arg_end(10),
  };

  port->ival[0] = DEFAULT_PORT;
  cluster->filename[0] = DEFAULT_CLUSTER;
  btxs->ival[0] = DEFAULT_TX_BUFFERING;
  log_width->ival[0] = (int)log_payload_width;

  int n_errors = arg_parse(argc, argv, argt);

  if (help->count > 0) {
    printf("Usage: %s", argv[0]);
    arg_print_syntax(stdout, argt, "\n");
    arg_print_glossary_gnu(stdout, argt);
    return EXIT_SUCCESS;
  }
  if (n_errors > 0) {
    arg_print_errors(stderr, end, argv[0]);
    fprintf(stderr, "Try %s --help?\n", argv[0]);
    return EXIT_FAILURE;
  }

  log_level += verbosity->count;
  log_payload_width = log_width->ival[0];

  if (chunk_size->count > 0) {
    if (chunk_size->ival[0] < 0) {
      fprintf(stderr, "chunk size cannot be negative\n");
      return EXIT_FAILURE;
    }
    knob.chunk_size = chunk_size->ival[0];
  }
  if (tx_size->count > 0) {
    if (tx_size->ival[0] < 0) {
      fprintf(stderr, "tx size cannot be negative\n");
      return EXIT_FAILURE;
    }
    knob.tx_size = max((size_t)tx_size->ival[0], knob.chunk_size);
  }
  if (btxs->ival[0] < 0) {
    fprintf(stderr, "cannot buffer up a negative number of txs\n");
    return EXIT_FAILURE;
  }
  knob.read_buffer_size = knob.tx_size * btxs->ival[0];

  uv_loop_t *loop = uv_default_loop();
  if (!ship_server_init(loop, port->ival[0]))
    return EXIT_FAILURE;

  return uv_run(loop, UV_RUN_DEFAULT);
}
