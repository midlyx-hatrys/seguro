#pragma once

#include "buffer.h"

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
      size_t h, t;
    } data;
  } event;
} db_write_op_t;

// client state header, after which we put the buffers
typedef struct client {
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
