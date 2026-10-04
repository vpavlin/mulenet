# 0008. v1 is uninspected: weigh and measure, declare, refuse - never open

- Status: Accepted
- Date: 2026-10-04

## Context

Hubs carry parcels they can't see into (OPEN-PROBLEMS 1). Inspection breaks the basic expectation of a parcel service and creates correlation points.

## Decision

Hubs never open anything. They check the outer sleeve's seal, the box class and the weight band against the sender's declaration, publish which categories they carry, and may refuse any parcel. The problem of protecting hubs from what they can't see is recorded as open, not solved.

## Rejected

Inspection at the entry hub; inspection at any hub that wants it; scanners (all considered in OPEN-PROBLEMS 1).

## Consequences

MuleNet is for test parcels between people who trust each other until this is resolved.
