# Seguro protocol version 0

## Overview

- No exceptional conditions are defined.  Event logging is a
  fundamental part of Urbit, and it is unclear whether a ship could
  conceivably handle an unforeseen failure thereof in any manner other
  than to just crash.  So any breach of protocol by any party for any
  reason should lead to disconnection and probably client crash.
  
- Except for the handshake, the protocol is mostly-async.
  
## Conventions

- All metadata is in string form -- it is not supposed to consume
  noticeable bandwidth, so we optimize for easier inspection.  Event
  data is streamed raw, braketed by declared length and two newlines.
  
- Metadata fields are separated by single spaces.  Metadata blocks
  end with newline.
  
- Numeric metadata fields must be strings in the format recognized by
  `strtoul(3)`/`strtoull(3)` (depending on target bit width) and
  wholly parseable.  Target bit width is specified in parentheses.
  
## Protocol

### Handshake

- Client initiates connection to server, server accepts.

#### [ Protocol version negotiation, must never change ]

- Server says `SEGURO` and enumerates all protocol versions it
  accepts.  Versions are numbers(16).

- Client says `HELLO` followed by the protocol version it wants to
  speak.  Version must be one of those accepted by server.

#### [ Everything from this point on may change in future versions ]

- Server says `IDENTIFY`, followed by protocol version.  Version must
  be the same as requested by client.

- Client says `POINT`, followed by its (hopefully) `@p`.
  
- If a connection with the same point already exists (TBD on how
  exactly server knows this, should probably be a DB-level
  keepalive record), server drops the session.
  
- Otherwise server says `WELCOME`, `@p`, id(64) of the latest event it
  has for the point (or nothing if it does not have any).  Point
  must be the same that client identified with.
  
- Server is now ready to serve flows for client.
  
### Flows

#### Writing many events

- Client says `SAVE MANY`, number(64) of events that follow, start
  id(64), end id(64).  Event ids must be greater than the latest event
  id stored for the ship, end id must be larger than start id.
  
- Client streams the events to server, where each event is `EVENT`,
  id(64), data length(64), newline, data (raw), two newlines.  Size of
  data must be exactly as declared.  Event ids must not repeat, must
  monotonically increase, start and end ids must match the `SAVE MANY`
  request fields, number of events must be as declared.
  
- After client is done, it says `STREAMED MANY`, number of events
  streamed, start id, end id.  The information must match the `SAVE
  MANY` request.

- Once all events are committed, server says `COMMITTED MANY`, number
  of events, start id, end id.  The information must match the `SAVE
  MANY` request.
  
- Server _may_ send notifications to the client as events are
  committed.  Those have the form `LATEST` followed by the highest
  committed event at the time.  The frequency of those notifications
  is left to server's discretion.  There is no requirement to notify
  about every committed event.  Event ids must not decrease and may
  repeat.  Note that the non-decreasing commited event id requirement
  holds across all notifications that carry it during the session:
  `LATEST`, `COMMITTED` `COMMITTED MANY` & `WELCOME`.
  
- Until the `COMMITTED MANY` notification is emitted, client must not
  issue any new commands.
  
#### Writing one event

- Client says `SAVE`, event id(64), data length(64), newline, data
  (raw), two newlines.  Size of data must be exactly as declared.
  Event id must be greater than the latest event id stored for the
  ship.
  
- Once the event is committed, server says `COMMITTED`, event id, data
  length.  The information must match the `SAVE` request.
  
- Until the `COMMITTED MANY` notification is emitted, client must not
  issue any new commands.
  
[ Strictly speaking this flow is redundant because writing one event
  can be done by writing an array of that one event.  But this flow is
  less chatty and so may sometimes be preferable ]

#### Reading events

- Client says `READ`, event id(64) to start with, limit(64).
  
- Server streams events to client starting with the requested id (or
  the first one above it, if that exact id does not exist in the
  database), at most the requested limit number of events, where each
  event is `EVENT`, id(64), data length(64), newline, data (raw), two
  newlines (just like in `SAVE MANY`).  Event ids must not repeat and
  must monotonically increase.
  
- After server is done, it says `SENT`, number(64) of events it
  actually sent, start id(64), end id(64).  Number must not exceed the
  requested limit and be the same as number of events actually sent,
  expected consistency requirements on ids apply.
  
- Until the `SENT` notification is emitted, client must not issue any
  new commands.
