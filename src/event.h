/// @file events.h
///
/// Event struct definitions and declarations for functions which manage events.

#pragma once

#include <foundationdb/fdb_c.h>
#include <stddef.h>
#include <stdint.h>

#define EXTENDED_HEADER 0x80
#define MAX_HEADER_SIZE 4

//==============================================================================
// Types
//==============================================================================

typedef struct event_t {
  uint64_t id;          // Unique, ordered identifier for event.
  uint64_t data_length; // Length of event data in bytes.
  uint8_t *data;        // Pointer to event data array.
} Event;

//==============================================================================
// Prototypes
//==============================================================================

/// Create the header for a fragmented event, which stores the number of
/// fragments of which an event is composed.
///
/// @param[in] header         Pointer to the byte[] to output the header.
/// @param[in] num_fragments  Number of fragments to encode in the header.
///
/// @return   The length of the header in bytes.
uint8_t build_header(uint8_t *header, uint32_t num_fragments);

/// Read the total number of fragments for an event from the header.
///
/// @param[in] header         Handle for the header.
/// @param[in] num_fragments  Address to write the number of fragments into.
///
/// @return   The length of the header in bytes
uint8_t read_header(const uint8_t *header, uint32_t *num_fragments);

/// Deallocate the heap memory used by an event.
///
/// @param[in] event  The event to deallocate.
void free_event(Event *event);

#ifndef container_of
#define container_of(ptr, type, member) \
  ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

typedef struct source_t {
  Event event;
  const struct source_ops_t *ops;
} Source;

void init_event_source(Source *es, Event *event, const struct source_ops_t *ops);

typedef struct source_ops_t {
  uint32_t (*length)(const Source *src);
  uint32_t (*num_fragments)(const Source *src);
  uint8_t (*header_length)(const Source *src);
  const uint8_t *(*header)(const Source *src);
  uint32_t (*prefix_length)(const Source *src);
  uint32_t (*fragment_length)(const Source *src, uint32_t fragment);
  uint8_t *(*fragment_data)(const Source *src, uint32_t fragment);
  void (*free)(Source *src);
} SourceOps;

static uint32_t __attribute__((used)) es_length(const Source *src) {
  return src->ops->length(src);
}
static uint32_t __attribute__((used)) es_num_fragments(const Source *src) {
  return src->ops->num_fragments(src);
}
static uint8_t __attribute__((used)) es_header_length(const Source *src) {
  return src->ops->header_length(src);
}
static const uint8_t *  __attribute__((used)) es_header(const Source *src) {
  return src->ops->header(src);
}
static uint32_t __attribute__((used)) es_prefix_length(const Source *src) {
  return src->ops->prefix_length(src);
}
static uint32_t __attribute__((used)) es_fragment_length(const Source *src, uint32_t fragment) {
  return src->ops->fragment_length(src, fragment);
}
static uint8_t * __attribute__((used)) es_fragment_data(const Source *src, uint32_t fragment) {
  return src->ops->fragment_data(src, fragment);
}
static void __attribute__((used)) es_free(Source *src) {
  src->ops->free(src);
}

typedef struct {
  uint32_t fragment_length;
  uint8_t header[MAX_HEADER_SIZE]; // Header for first fragment which encodes
                                   // the number of fragments.
  uint8_t header_length;           // Length of header in bytes.
  Source src;
} FragmentedEventSource;

void init_fragmented_event_source(FragmentedEventSource *es,
                                  Event *event,
                                  uint32_t fragment_length);
