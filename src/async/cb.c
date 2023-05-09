#include "cb.h"
#include <stdlib.h>
#include <string.h>

static inline uintptr_t e2b(const cb_t *cb, size_t i) {
  return i * cb->esize;
}

bool cb_init(cb_t *cb, size_t size, size_t esize, bool growable) {
  size = 1UL << (sizeof(size_t) * 8 - __builtin_clzl(size - 1));
  cb->b = aligned_alloc(size, size);
  if (!cb->b)
    return false;

  cb->mask = size - 1;
  cb->esize = esize;
  cb->r = cb->w = 0;
  cb->growable = growable;
  return true;
}

void cb_destroy(cb_t *cb) {
  free(cb->b);
  cb->b = NULL;
}

bool cb_grow(cb_t *cb) {
  size_t size = cb_size(cb);
  uintptr_t r = cb_r(cb);
  uintptr_t w = cb_w(cb);

  size_t nsize = size << 1;
  uint8_t *nb = aligned_alloc(nsize, nsize);
  if (!nb)
    return false;

  if (r <= w) {
    memcpy(nb + e2b(cb, r), cb->b + e2b(cb, r), e2b(cb, w - r));
  } else {
    memcpy(nb + e2b(cb, r), cb->b + e2b(cb, r), e2b(cb, size - r));
    memcpy(nb + e2b(cb, size), cb->b, e2b(cb, w));
    cb->w += size;
  }

  free(cb->b);
  cb->b = nb;
  cb->mask = nsize - 1;
  return true;
}

void cb_copy(const cb_t *cb, void *target, size_t h, size_t t) {
  size_t size = cb_size(cb);
  h = cb_mask(cb, h);
  t = cb_mask(cb, t);

  if (h <= t) {
    memcpy(target, cb->b + e2b(cb, h), e2b(cb, t - h));
  } else {
    memcpy(target, cb->b + e2b(cb, h), e2b(cb, size - h));
    memcpy((uint8_t *)target + e2b(cb, size - h), cb->b, e2b(cb, t));
  }
}

void *cb_enq(cb_t *cb) {
  return cb_full(cb) && (!cb->growable || !cb_grow(cb)) ? NULL
      : cb->b + (cb->w++ & cb->mask) * cb->esize;
}
