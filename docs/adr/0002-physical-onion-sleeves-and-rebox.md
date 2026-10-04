# 0002. Physical onion: one sealed sleeve per hop, re-box every hop

- Status: Accepted
- Date: 2026-10-04

## Context

Re-boxing alone isn't enough: the item itself travels through every hop, so two colluding hubs either side of an honest hub could match it by photo.

## Decision

The sender wraps the core in one tamper-sealed sleeve per hop. Hop i removes sleeve i (checking its seal code against its slot) and ships the rest in a fresh standard box padded to the weight band. Sleeve i is seen only by hop i-1 (which exposes it) and hop i.

## Rejected

Re-boxing only (item-level correlation); hubs re-packing the item themselves (requires opening).

## Consequences

Senders pack N sleeves; the packing list tells them the order. The sleeve weight is declared per hop so the padding stays constant.
