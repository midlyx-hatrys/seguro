# Seguro Phase 3

## What

Seguro is a network (or local) service that provides reliable log
storage and replay to Urbit ships.

## Objectives

- Specify the Seguro protocol.

- Implement a Seguro server plus some clients for testing,
  benchmarking and reference purposes.

- The implementation must be capable of serving multiple Urbit ships
  by the same Seguro instance.  The code-base must be in a state that
  is ready to use by Vere, once Vere implements the client side of the
  protocol and has a way to point it at a Seguro instance.

## Background

By end of Phase 2 there is a working self-contained prototype that is
(logically) both a server and testing/benchmarking client.  It
provides a code base of useful primitives and valuable benchmarking
results.  We use it as a starting point and reference for implementing
a performant Seguro server.

## Network protocol

See [protocol](protocol.md).

## DB schema

See [schema](FDB-schema.md).

## Implementation notes

Seguro continues to require FoundationDB, version 7.1 or newer.  Some
changes are made to the storage schema relative to Phase 2 in order to
make stored data completely self-describing (no implicit constants
like "optimal fragment size"), versioned, and per-ship.

We might or might not use the "experimental" FoundationDB tenant
feature for ship isolation, or perhaps it will be a run-time option.

Initial implementation is being done in plain C (actually
`-std=gnu11`, as required by libuv, but that's plain enough in
practice) using [libuv](https://github.com/libuv/libuv) and the
official FoundationDB C bindings, with no other significant
third-party dependencies (other language stacks were excluded from
consideration due to various reasons or combinations thereof, such as:
lack of officially-supported FoundationDB bindings, deployment
complexity, unpredictable performance, relative lack of familiarity,
prejudice).

Provided testing/benchmarking clients will probably be written in
Python.

## Future directions
  
- Events are currently presumed stored from the point a ship starts
  using Seguro, forever.  Support for trimming logs and for storing
  snapshots is desirable, but is postponed until the Epoch work is
  concluded and it is clear(er) what exactly needs to be supported and
  how.
  
- The initial protocol version is stream-based.  For same-machine use
  a shared-memory-based variant would make a lot of sense, as it would
  avoid much redundant copying.
  
- With some small changes the protocol could be made into a base for a
  hot-failover Urbit availability setup (it's probably good enough for
  cold-failover use as is, as the event id consistency checks should
  provide sufficient double-boot protection).
