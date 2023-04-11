/// @file events.c
///
/// Definitions for functions which manage events.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "event.h"

//==============================================================================
// Functions
//==============================================================================

uint8_t build_header(uint8_t *header, uint32_t num_fragments) {
  // HEADER   MAX # FRAGS   MAX EVENT SIZE
  // 1 byte   128                1280000 bytes (  1.28 MB)
  // 2 byte   256                2560000 bytes (  2.56 MB)
  // 3 byte   65536            655360000 bytes (655.36 MB)
  // 4 byte   16777216      167772160000 bytes (~168 GB)
  // ...
  //
  // Thus, the header size is determined by the number of fragments necessary to
  // store the event (which is indirectly tied to the OPTIMAL_VALUE_SIZE macro
  // [10,000 bytes for FDB]). We can also set an upper limit using the
  // MAX_HEADER_SIZE macro for event sizes we expect to never be reached (e.g.
  // maximum header size of 4 if we never expect to see an event larger than
  // ~168GB).
  //
  if (num_fragments < 128) {
    header[0] = ((uint8_t *)&num_fragments)[0];
    return 1;
  }

  for (uint8_t i = 1; i < (MAX_HEADER_SIZE - 1); ++i) {
    if (num_fragments < ((uint32_t)(1 << (i * 8)))) {
      header[0] = (EXTENDED_HEADER | i);
      memcpy((header + 1), &num_fragments, i);
      return (i + 1);
    }
  }

  // Technically this is wrong if num_fragments >= 2^24
  header[0] = (EXTENDED_HEADER | (MAX_HEADER_SIZE - 1));
  memcpy((header + 1), &num_fragments, (MAX_HEADER_SIZE - 1));
  return MAX_HEADER_SIZE;
}

uint8_t read_header(const uint8_t *header, uint32_t *num_fragments) {
  if (header[0] & EXTENDED_HEADER) {
    uint8_t header_bytes = (header[0] ^ EXTENDED_HEADER);
    memcpy((uint8_t *)num_fragments, (header + 1), header_bytes);

    return (header_bytes + 1);
  }

  *num_fragments = header[0];
  return 1;
}

void free_event(Event *event) { free((void *)event->data); }

void init_event_source(Source *es, Event *event, const struct source_ops_t *ops) {
  es->event = *event;
  // consume the event
  event->data = NULL;
  es->ops = ops;
}

uint32_t f_event__length(const Source *src) {
  return src->event.data_length;
}

uint32_t f_event__num_fragments(const Source *src) {
  FragmentedEventSource *es = container_of(src, FragmentedEventSource, src);
  uint32_t rem = src->event.data_length % es->fragment_length;
  uint32_t div = src->event.data_length / es->fragment_length;
  return rem == 0 ? div : div + 1;
}

uint32_t f_event__prefix_length(const Source *src) {
  FragmentedEventSource *es = container_of(src, FragmentedEventSource, src);
  uint32_t ret = src->event.data_length % es->fragment_length;
  if (ret == 0)
    ret = es->fragment_length;
  return ret;
}

uint8_t f_event__header_length(const Source *src) {
  return container_of(src, FragmentedEventSource, src)->header_length;
}

const uint8_t *f_event__header(const Source *src) {
  return container_of(src, FragmentedEventSource, src)->header;
}

uint32_t f_event__fragment_length(const Source *src, uint32_t fragment) {
  FragmentedEventSource *es = container_of(src, FragmentedEventSource, src);
  return fragment == 0 ? f_event__prefix_length(src) : es->fragment_length;
}

uint8_t *f_event__fragment_data(const Source *src, uint32_t fragment) {
  FragmentedEventSource *es = container_of(src, FragmentedEventSource, src);

  uint32_t prefix_length = f_event__prefix_length(src);
  return src->event.data + (fragment == 0 ? 0
                            : prefix_length + (fragment - 1) * es->fragment_length);
}

void f_event__free(Source *src) {
  free_event(&src->event);
}

SourceOps f_event_ops = {
  .length = f_event__length,
  .num_fragments = f_event__num_fragments,
  .prefix_length = f_event__prefix_length,
  .header_length = f_event__header_length,
  .header = f_event__header,
  .fragment_length = f_event__fragment_length,
  .fragment_data = f_event__fragment_data,
  .free = f_event__free
};

void init_fragmented_event_source(FragmentedEventSource *es,
                                  Event *event,
                                  uint32_t fragment_length) {
  init_event_source(&es->src, event, &f_event_ops);
  es->fragment_length = fragment_length;

  // Header encodes number of ADDITIONAL fragments
  es->header_length = build_header(es->header, f_event__num_fragments(&es->src) - 1);
}
