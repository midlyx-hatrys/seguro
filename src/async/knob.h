#pragma once

#include <stddef.h>

typedef struct {
  size_t tx_size;
  size_t chunk_size;
  size_t read_buffer_size;
} knobs_t;

extern knobs_t knob;
