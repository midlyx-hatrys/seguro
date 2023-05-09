#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct cb {
  uint8_t *b;
  uintptr_t r, w;
  uintptr_t mask;
  size_t esize;
  bool growable;
} cb_t;

bool cb_init(cb_t *cb, size_t size, size_t esize, bool growable);
void cb_destroy(cb_t *cb);
bool cb_grow(cb_t *cb);

static inline uintptr_t cb_mask(const cb_t *cb, uintptr_t i) {
  return i & cb->mask;
}

static size_t cb_r(const cb_t *cb) {
  return cb_mask(cb, cb->r);
}
static size_t cb_w(const cb_t *cb) {
  return cb_mask(cb, cb->w);
}

static inline bool cb_empty(const cb_t *cb) {
  return cb_r(cb) == cb_w(cb);
}
static inline bool cb_full(const cb_t *cb) {
  return cb_mask(cb, cb->w + 1) == cb_r(cb);
}

static inline size_t cb_size(const cb_t *cb) {
  return cb->mask + 1;
}
static inline size_t cb_esize(const cb_t *cb) {
  return cb->esize;
}
static inline size_t cb_occupied(const cb_t *cb) {
  return cb_mask(cb, cb->w - cb->r);
}
static inline size_t cb_free(const cb_t *cb) {
  return cb_mask(cb, cb->r - cb->w - 1);
}

static inline size_t cb_cr_headroom(const cb_t *cb) {
  uintptr_t r = cb_r(cb);
  uintptr_t w = cb_w(cb);
  return r <= w ? w - r : cb_size(cb) - r;
}

static inline size_t cb_cw_headroom(const cb_t *cb) {
  uintptr_t r = cb_r(cb);
  uintptr_t w = cb_w(cb);
  return w < r ? r - w - 1 : cb_size(cb) - w;
}

static inline void *cb_elem_ptr(const cb_t *cb, size_t i) {
  return cb->b + (cb_mask(cb, i) * cb->esize);
}
static inline const void *cb_r_ptr(const cb_t *cb) {
  return cb->b + cb_r(cb) * cb->esize;
}
static inline void *cb_w_ptr(const cb_t *cb) {
  return cb->b + cb_w(cb) * cb->esize;
}

static inline void cb_r_bump(cb_t *cb, size_t by) {
  cb->r += by;
}
static inline void cb_w_bump(cb_t *cb, size_t by) {
  cb->w += by;
}

void *cb_enq(cb_t *cb);
static inline void *cb_deq(cb_t *cb) {
  return cb_empty(cb) ? NULL: cb->b + (cb->r++ & cb->mask) * cb->esize;
}

void cb_copy(const cb_t *cb, void *target, size_t h, size_t t);
