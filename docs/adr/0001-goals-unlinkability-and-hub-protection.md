# 0001. Goals: sender-recipient unlinkability and hub protection

- Status: Accepted
- Date: 2026-10-04

## Context

MuleNet is a physical mixnet. Two goals were set by the project owner: nobody but the sender can link sender and recipient, and the hubs (friends, Logos Circle members) are protected as much as possible.

## Decision

Optimise for both. Every later decision is checked against both goals; where they conflict (contents inspection) we stop and record the problem instead of picking one silently (OPEN-PROBLEMS.md 1).

## Rejected

Contents secrecy as a third goal on equal footing (it is a consequence of not inspecting, not a goal); anonymity against a global postal observer (not achievable at friend-network volume).

## Consequences

The threat model (THREAT-MODEL.md) is written from these two goals. The design accepts that the trust graph and hub cities are public.
