# 0011. Trust: you are your own root; stewards vouch for hubs

- Status: Accepted
- Date: 2026-10-04

## Context

Anyone can announce a hub. Routes must only use hubs someone the sender trusts has vetted.

## Decision

Each user has trust roots (themselves by default) and can add stewards' addresses. Stewards can appoint stewards (`steward.add`) and vouch for hubs. A hub is eligible when at least one of your stewards vouches for it. Logos Circle stewards are the natural first stewards.

## Rejected

A global allow-list (central); token-gating (parked; maybe later, privately, via the LEZ ZK work).

## Consequences

Two users with different roots can see different eligible hubs. That is intended.
