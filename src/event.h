//! @file event.h
//!
//! Events header file.

#ifndef EVENT_H
#define EVENT_H

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef FDB_API_VERSION
#define FDB_API_VERSION 710
#include <foundationdb/fdb_c.h>
#endif


//==============================================================================
// Types
//==============================================================================

//! Raw event object (needs fragmentation before insertion into FoundationDB).
typedef struct event_t {
  uint8_t key;          //! Unsigned integer key.
  char*   value;        //! Byte buffer value.
  int     value_length; //! Length of the value's byte array.
} Event;

//==============================================================================
// Prototypes
//==============================================================================

//! Writes raw, non-fragmented, mock events into the given array.
//!
//! @param[in] num_events  The number of events to write into an array in memory.
//! @param[in] size        The size of each event, in bytes.
//!
//! @return       Events array.
//! @return NULL  Failure.
Event* load_mock_events(int num_events, int size);

//! Reads events from LMDB and writes them into an array in memory.
//!
//! @param[in] mdb_file    The .mdb file to read from.
//! @param[in] num_events  The number of events to write into the event array.
//! @param[in] size        The size of each event, in bytes.
//!
//! @return       Events array.
//! @return NULL  Failure.
Event* load_lmdb_events(char* mdb_file, int num_events, int size);

//! Counts the number of digits in the given integer.
//!
//! @param[in] n  The integer.
//!
//! @return  The number of digits.
int count_digits(int n);

#endif