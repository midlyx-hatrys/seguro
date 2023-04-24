# Seguro FDB schema version 0

## Overview

- A point's keyspace has (conceptually) two sections: data and
  metadata.
  
- The schema does not rely on any implicit assumptions or magic
  constants, such that the stored data is completely self-describing
  and also Seguro implementations have some amount of flexibility.
  
- You may note an incompatible change relative to Phase II schema: we
  store small (non-fragmented) events as-is, and drop the elaborate
  first-fragment-header scheme.
  
## Conventions

- All keys are prefixed by `0x00`, to avoid encroaching on the FDB
  reserved keyspace (which is under `0xFF`).  We omit this detail for
  brevity below.
  
- All keys are either prefixed with point number or are under the
  point tenant.  We omit this below as well.
  
## Point segregation

- If using subspaces, all keys for a point are prefixed with the point
  number, in little-endian encoding with trailing zeros stripped
  (i.e. `~midlyx-hatrys`, which is `0x002d0400`, becomes `0040d2`,
  saving 2 whole bytes per key.  `~zod`'s prefix is empty).
  
- If using tenants, those are named after their patps (no call for
  compression in that case; also `~zod`'s tenant name would be empty
  otherwise, which might not be legal).
  
## Metadata

Metadata is under the `meta` sub(sub)space.  The relevant keys are:

- `meta/schema-version` -- `0`.

- `meta/latest` -- the id of the latest stored event for the
  point.
  
## Data

All data records store ship events, so all begin with the event id.
The id is encoded as 64-bit big-endian (important for sorting).

We define 3 kinds of records, which are distinguished by the
descriptor byte right after the event id, or lack thereof.

### Whole events

Those are used to store events that are small enough to be stored whole, which should be
most of them.

- Key suffix: absent.
- Value: event data.

### Event fragments

- Key suffix: `0x00`, fragment number (64-bit big-endian, because it
  is sorted on).  If fragment number is `0`, it is followed by the
  total event size (64-bit little-endian).
- Value: fragment data.

Sum of all fragment data sizes for one event must be the total size
declared by the first fragment's key.

### Special events

[ This is mostly an idea to facilitate encoding "indirect" or "fake"
  events, like for example if Seguro is extended to store snapshots
  too, and those are written to an S3 bucket (the value will then
  encode the URL), or written in background to the same FDB cluster
  under a unique prefix prior to committing as event (the value will
  then contain that prefix). ]
  
- Key suffix: a byte that `OR`'s with 0x1.  The other 7 bits are
  available to Seguro so it can distinguish things further.
- Value: opaque data.
