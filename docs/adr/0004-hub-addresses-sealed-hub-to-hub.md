# 0004. Hub addresses are granted hub-to-hub, sealed

- Status: Accepted
- Date: 2026-10-04

## Context

To ship to the next hop a hub needs its address. If the sender builds that into the label, every sender learns every hub's address - the main threat to hubs.

## Decision

Hub B publishes `hub.grant {to: A, ref, sealed}`: B's handoff address sealed to A's box key. The sender only puts the grant ref in A's slot. Only A can read B's address; B chooses who may ship to it and can revoke. Senders hand parcels over at a public intake (a meetup, a public locker).

## Rejected

Addresses in the label (senders learn all addresses); one network-wide key for addresses (any hub learns all addresses); a relay service (a central point of failure and of knowledge).

## Consequences

The graph of who granted whom is public. Planning only uses edges that exist. Grants are N^2 in the worst case, fine for a friends network.
