#pragma once

#include <stddef.h>

typedef struct {
  size_t max_transaction_size;
  size_t max_chunk_size;
  size_t read_buffer_size;
} knobs_t;

extern knobs_t knob;
