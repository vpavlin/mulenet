# 0006. Custody receipts: one topic, token-tagged, AEAD

- Status: Accepted
- Date: 2026-10-04

## Context

The sender wants progress, a hub wants an alibi ("the next hub received it intact"), and nobody else should learn anything about a route. Per-parcel topics would require joining many topics on a shared node.

## Decision

Every slot carries custodyIn/custodyOut tokens shared with the neighbours. Receipts go on one topic, `/mulenet/1/receipts/proto`, as tag(16) | nonce | ChaCha20-Poly1305; the tag and key derive from the token. Day resolution, posted after a random 2-30 h delay. Receipts are unsigned events with id = hash(bytes), so RBSR backfills them; 90-day retention.

## Rejected

Per-token topics (subscription churn, and the topic itself is a correlator on the wire); signed receipts (a signature identifies the hub to everyone who can read it).

## Consequences

Anyone stores all receipts (small); only token holders can read or even group them.
