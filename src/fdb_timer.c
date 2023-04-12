/// @file fdb_timer.c
///
/// Additions/modifications to fdb.c for performing timed benchmarks.

#include <foundationdb/fdb_c.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include "fdb.h"
#include "fdb_timer.h"

//==============================================================================
// Variables
//==============================================================================

extern uint32_t fdb_batch_size;
thread_local FDBTimer timer_sync = {(clock_t)INT_MAX, (clock_t)0, 0.0};

//==============================================================================
// Prototypes
//==============================================================================

/// Callback function for when an asynchronous FoundationDB transaction is
/// applied successfully.
///
/// @param[in] future   Handle for the FoundationDB future.
/// @param[in] t_start  Contains the time at which the tx was launched.
void write_callback(FDBFuture *future, void *t_start);

/// Callback function for when an asynchronous FoundationDB transaction is
/// applied successfully.
///
/// @param[in] future  Handle for the FoundationDB future.
/// @param[in] cbd     Handle for an FDBCallbackData object.
void write_callback_async(FDBFuture *future, void *cbd);

/// Callback function for when the FoundationDB database is cleared.
///
/// @param[in] future    Handle for the FoundationDB future.
/// @param[in] settings  Handle for the BenchmarkSettings object.
void clear_callback(FDBFuture *future, void *settings);

/// Callback function for clearing the database after asynchronous writes.
///
/// @param[in] future    Handle for the FoundationDB future
/// @param[in] settings  Handle for the BenchmarkSettings object.
void clear_callback_async(FDBFuture *future, void *settings);

/// Resets the global benchmark timer.
void reset_timer(void);

/// Count the number of total fragments in an array of fragmented events.
///
/// @param[in]  events      Pointer to the array of FragmentedEvent objects.
/// @param[in]  num_events  Number of events in the array.
///
/// @return  Number of total fragments.
uint32_t total_fragments(const FragmentedEventSource *events, uint32_t num_events);

//==============================================================================
// Functions
//==============================================================================

int fdb_send_timed_transaction(FDBTransaction *tx,
                               FDBCallback callback_function,
                               void *callback_param) {
  // Commit transaction
  FDBFuture *future = fdb_transaction_commit(tx);

  // Register callback
  if (fdb_check_error(
          fdb_future_set_callback(future, callback_function, callback_param)))
    goto tx_fail;

  // Wait for the future to be ready
  if (fdb_check_error(fdb_future_block_until_ready(future)))
    goto tx_fail;

  // Check that the future did not return any errors
  if (fdb_check_error(fdb_future_get_error(future)))
    goto tx_fail;

  // Destroy the future
  fdb_future_destroy(future);

  // Delete existing transaction object and create a new one
  fdb_transaction_reset(tx);

  // Success
  return 0;

// Failure
tx_fail:
  return -1;
}

int fdb_timed_write_event_array(const FragmentedEventSource *events, uint32_t num_events) {
  FDBTransaction *tx;
  clock_t *start_t;
  uint32_t batch_filled = 0;
  uint32_t frag_pos = 0;
  uint32_t i = 0;

  // Initialize transaction
  if (fdb_check_error(fdb_setup_transaction(&tx)))
    goto tx_fail;

  // For each event
  while (i < num_events) {
    // Add as many unwritten fragments from the current event as possible
    // (method differs slightly depending on whether there are already other
    // fragments in the batch)
    if (!batch_filled) {
      batch_filled = add_event_set_transactions(tx, &events[i].src, frag_pos,
                                                fdb_batch_size);
      frag_pos += batch_filled;
    } else {
      uint32_t num_kvp = add_event_set_transactions(
          tx, &events[i].src, frag_pos, (fdb_batch_size - batch_filled));
      batch_filled += num_kvp;
      frag_pos += num_kvp;
    }

    // Increment event counter when all fragments from an event have been
    // written
    if (frag_pos == es_num_fragments(&events[i].src)) {
      i += 1;
      frag_pos = 0;
    }

    // Attempt to apply transaction when batch is filled
    if (batch_filled == fdb_batch_size) {
      // Start timer just before committing transaction
      start_t = malloc(sizeof(clock_t));
      *start_t = clock();

      if (fdb_check_error(fdb_send_timed_transaction(
              tx, (FDBCallback)&write_callback, (void *)start_t)))
        goto tx_fail;

      batch_filled = 0;
    }
  }

  // Catch the final, non-full batch
  start_t = malloc(sizeof(clock_t));
  *start_t = clock();
  if (fdb_check_error(fdb_send_timed_transaction(
          tx, (FDBCallback)&write_callback, (void *)start_t)))
    goto tx_fail;

  // Clean up the transaction
  fdb_transaction_destroy(tx);

  // Success
  return 0;

// Failure
tx_fail:
  return -1;
}

int fdb_timed_write_event_array_async(const FragmentedEventSource *events,
                                      uint32_t num_events) {
  uint32_t i = 0;
  uint32_t b = 0;
  uint32_t batch_filled = 0;
  uint32_t frag_pos = 0;
  uint32_t total_frags = total_fragments(events, num_events);
  uint32_t num_batches = (uint32_t)ceil(total_frags / fdb_batch_size);
  uint32_t txs_processing = num_batches;
  FDBTransaction *txs[num_batches];
  FDBFuture *futures[num_batches];
  clock_t *timers[num_batches];
  FDBTimer timer = {(clock_t)INT_MAX, (clock_t)0, 0.0};
  FDBCallbackData *cbd = malloc(sizeof(FDBCallbackData));
  clock_t thread_start = clock();

  for (uint32_t j = 0; j < num_batches; j++)
    if (fdb_check_error(fdb_setup_transaction(&txs[j]))) {
      printf("tx_fail\n");
      goto tx_fail;
    }

  while (i < num_events) {
    if (batch_filled == 0) {
      batch_filled = add_event_set_transactions(txs[b], &events[i].src, frag_pos,
                                                fdb_batch_size);
      frag_pos += batch_filled;
    } else {
      uint32_t num_kvp = add_event_set_transactions(
          txs[b], &events[i].src, frag_pos, (fdb_batch_size - batch_filled));
      batch_filled += num_kvp;
      frag_pos += num_kvp;
    }

    if (frag_pos == es_num_fragments(&events[i].src)) {
      i++;
      frag_pos = 0;
    }

    if (batch_filled == fdb_batch_size) {
      cbd = malloc(sizeof(FDBCallbackData));
      timers[b] = malloc(sizeof(clock_t));
      *(timers[b]) = clock();
      cbd->start_t = timers[b];
      cbd->tx = txs[b];
      cbd->txs_processing = &txs_processing;
      cbd->timer = &timer;
      cbd->num_events = num_events;
      cbd->num_frags = total_frags;
      cbd->batch_size = fdb_batch_size;

      futures[b] = fdb_transaction_commit(txs[b]);
      if (fdb_check_error(fdb_future_set_callback(
              futures[b], (FDBCallback)&write_callback_async, (void *)cbd)))
        goto tx_fail;

      batch_filled = 0;

      if (b < (num_batches - 1))
        b++;
    }
  }

  if (b < (num_batches - 1)) {
    cbd = malloc(sizeof(FDBCallbackData));
    timers[b] = malloc(sizeof(clock_t));
    *(timers[b]) = clock();
    cbd->start_t = timers[b];
    cbd->tx = txs[b];
    cbd->txs_processing = &txs_processing;
    cbd->timer = &timer;
    cbd->num_events = num_events;
    cbd->num_frags = total_frags;
    cbd->batch_size = fdb_batch_size;

    futures[b] = fdb_transaction_commit(txs[b]);
    if (fdb_check_error(fdb_future_set_callback(
            futures[b], (FDBCallback)&write_callback_async, (void *)cbd)))
      goto tx_fail;
  }

  while (0 != txs_processing) {
    // Wait for all txs to finish
  }

  clock_t thread_end = clock();
  double thread_total =
      ((((double)(thread_end - thread_start)) / CLOCKS_PER_SEC) * 1000.0);

  // Print times
  printf("    thread  %12f ms\n", (thread_total));
  printf(" avg/event  %12f ms\n", (thread_total / num_events));
  printf(" max batch  %12f ms\n", (1000.0 * timer.t_max / CLOCKS_PER_SEC));
  printf(" avg batch  %12f ms\n", (thread_total / num_batches));
  printf(" min batch  %12f ms\n", (1000.0 * timer.t_min / CLOCKS_PER_SEC));

  // Success
  return 0;

// Failure
tx_fail:
  return -1;
}

int fdb_clear_timed_database(uint32_t num_events, uint32_t num_fragments) {
  BenchmarkSettings *settings = malloc(sizeof(BenchmarkSettings));
  FDBTransaction *tx;
  uint8_t start_key[1] = {0};
  uint8_t end_key[1] = {0xFF};

  // Initialize transaction
  if (fdb_check_error(fdb_setup_transaction(&tx)))
    goto tx_fail;

  // Add clear operation to transaction
  fdb_transaction_clear_range(tx, start_key, 1, end_key, 1);

  // Setup settings
  settings->num_events = num_events;
  settings->num_frags = num_fragments;
  settings->batch_size = fdb_batch_size;

  // Catch the final, non-full batch
  if (fdb_send_timed_transaction(tx, (FDBCallback)&clear_callback,
                                 (void *)settings))
    goto tx_fail;

  // Clean up the transaction
  fdb_transaction_destroy(tx);

  // Success
  return 0;

// Failure
tx_fail:
  return -1;
}

int fdb_clear_timed_database_async(uint32_t num_events,
                                   uint32_t num_fragments) {
  FDBTransaction *tx;
  uint8_t start_key[1] = {0};
  uint8_t end_key[1] = {0xFF};

  // Initialize transaction
  if (fdb_check_error(fdb_setup_transaction(&tx)))
    goto tx_fail;

  // Add clear operation to transaction
  fdb_transaction_clear_range(tx, start_key, 1, end_key, 1);

  FDBFuture *future = fdb_transaction_commit(tx);
  if (fdb_check_error(fdb_future_block_until_ready(future)))
    goto tx_fail;

  // Check that the future did not return any errors
  if (fdb_check_error(fdb_future_get_error(future)))
    goto tx_fail;

  // Destroy the future
  fdb_future_destroy(future);

  // Clean up the transaction
  fdb_transaction_destroy(tx);

  // Success
  return 0;

// Failure
tx_fail:
  return -1;
}

void write_callback(FDBFuture *future, void *param) {
  // Output timing stats
  clock_t t_start = *((clock_t *)param);
  clock_t t_end = clock();
  clock_t t_diff = (t_end - t_start);
  double total_time = 1000.0 * t_diff / CLOCKS_PER_SEC;

  if (t_diff < timer_sync.t_min) {
    timer_sync.t_min = t_diff;
  }

  if (t_diff > timer_sync.t_max) {
    timer_sync.t_max = t_diff;
  }

  timer_sync.t_total += total_time;

  // Clean up timer from callback, or else race condition
  free(param);
}

void write_callback_async(FDBFuture *future, void *param) {
  FDBCallbackData *cbd = (FDBCallbackData *)param;

  clock_t t_start = *(cbd->start_t);
  clock_t t_end = clock();
  clock_t t_diff = (t_end - t_start);
  FDBTimer *timer = cbd->timer;
  double total_time = 1000.0 * t_diff / CLOCKS_PER_SEC;
  if (t_diff < timer->t_min) {
    timer->t_min = t_diff;
  }
  if (t_diff > timer->t_max) {
    timer->t_max = t_diff;
  }
  timer->t_total += total_time;

  fdb_future_destroy(future);
  (*cbd->txs_processing)--;
  fdb_transaction_destroy(cbd->tx);

  uint32_t txp = *(cbd->txs_processing);
  if (txp != 0) { // don't free last tx's cbd
    free(cbd->start_t);
    free(cbd);
  }
}

void clear_callback(FDBFuture *future, void *param) {
  BenchmarkSettings *settings = (BenchmarkSettings *)param;
  uint32_t num_batches =
      (settings->num_events * settings->num_frags) / settings->batch_size;

  printf("    thread  %12f ms\n", (timer_sync.t_total));
  printf(" avg/event  %12f ms\n", (timer_sync.t_total / settings->num_events));
  printf(" max batch  %12f ms\n", (1000.0 * timer_sync.t_max / CLOCKS_PER_SEC));
  printf(" avg batch  %12f ms\n", (timer_sync.t_total / num_batches));
  printf(" min batch  %12f ms\n", (1000.0 * timer_sync.t_min / CLOCKS_PER_SEC));

  // Clean up settings from callback, or else race condition
  free(param);

  reset_timer();
}

void reset_timer(void) {
  timer_sync.t_min = (clock_t)INT_MAX;
  timer_sync.t_max = (clock_t)0;
  timer_sync.t_total = 0.0;
}

uint32_t total_fragments(const FragmentedEventSource *events, uint32_t num_events) {
  uint32_t sum = 0;
  for (uint32_t i = 0; i < num_events; i++) {
    sum += es_num_fragments(&events[i].src);
  }
  return sum;
}
