# 0007. The hub directory is a signed loam-sync event log over Loam

- Status: Accepted
- Date: 2026-10-04

## Context

Hubs, grants, vouches and mailboxes must reach everyone, work offline and converge, with no server.

## Decision

loam-sync envelopes signed with secp256k1 (domain `mulenet`), merged by id and HLC, folded deterministically (LWW cards, tombstones last), synced by RBSR catch-up on `/mulenet/1/registry/proto` via loam_core. Each reader's trust roots decide whose vouches count.

## Rejected

A web directory (central, censorable); a LEZ program (premature; payments later).

## Consequences

Catch-up every 2 minutes plus a 3/10/25 s ladder after connecting: each SDS frame is ~19 KB and counts against the shared node's RLN budget.
