#pragma once

#include "buffer.h"
#include "log.h"

#include <gmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <uv.h>

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

#define NAME_AND_COMMA(TOKEN) TOKEN ,
#define STRING_AND_COMMA(TOKEN) #TOKEN ,

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
