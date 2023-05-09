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
  noticeable bandwidth, so we optimize for easier inspection.  Events
  themselves are streamed as newts.
  
- Metadata fields are separated by single spaces.  Metadata blocks
  end with the null character.
  
- Numeric metadata fields must be strings in the format recognized by
  `scanf/strtoul(3)`/`strtoull(3)` (depending on target bit width) and
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
  
- Otherwise server says `WELCOME`, `@p`, id(64) of the highest event
  it has for the point (or nothing if it does not have any).  Point
  must be the same that client identified with.
  
- Server is now ready to serve flows for client.
  
### Flows

#### Writing many events

- Client says `WRITE BATCH`, number(64) of events that follow, start
  id(64), end id(64).  Event ids must be greater than the latest event
  id stored for the ship, end id must be larger than start id.
  
- Client streams the events to server, where each event is `EVENT`,
  id(64), newt, null character.  Event ids must not repeat, must
  monotonically increase, start and end ids must match the `SAVE MANY`
  request fields, number of events must be as declared.
  
- After client is done, it says `END BATCH`, number of events
  streamed, start id, end id.  The information must match the `WRITE
  BATCH` request.

- Once all events are committed, server says `COMMITTED BATCH`, number
  of events, start id, end id.  The information must match the `WRITE
  BATCH` request.
  
- Server _may_ send progress notifications to the client as events are
  committed.  Those have the form `HEIGHT` followed by the highest
  committed event at the time.  The frequency of those notifications
  is left to server's discretion.  There is no requirement to notify
  about every committed event.  Event ids must not decrease and may
  repeat.  Note that the non-decreasing commited event id requirement
  holds across all notifications that carry it during the session:
  `HIGHT`, `COMMITTED` `COMMITTED BATCH` & `WELCOME`.
  
- Until the `COMMITTED BATCH` notification is emitted, client may not
  initiate new flows.
  
#### Writing one event

- Client says `WRITE`, event id(64), newt, null character.  Event id
  must be higher than the highest event id stored for the ship.
  
- Once the event is committed, server says `COMMITTED`, event id, data
  length.  The information must match the `SAVE` request.
  
- Until the `COMMITTED` notification is emitted, client may not
  initiate new flows.
  
[ Strictly speaking this flow is redundant because one event is the
  same as a single-event batch.  But this flow is less chatty and so
  may sometimes be preferable ]

#### Reading events

- Client says `READ`, event id(64) to start from, limit(64).
  
- Server streams events to client starting with the requested id (or
  the first one above it, if that exact id does not exist in the
  database), at most the requested limit number of events, where each
  event is `EVENT`, id(64), newt, null character (i.e. same as in the
  other direction).  Event ids must not repeat and must monotonically
  increase.
  
- After server is done, it says `SENT`, number(64) of events it
  actually sent, start id(64), end id(64).  Number must not exceed the
  requested limit and be the same as number of events actually sent,
  expected consistency requirements on ids apply.
  
- Until the `SENT` notification is emitted, client may not initiate
  new flows.
