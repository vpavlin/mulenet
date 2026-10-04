# 0005. Recipients create mailboxes at an exit hub

- Status: Accepted
- Date: 2026-10-04

## Context

The last hop needs the pickup point; the sender shouldn't need to know it.

## Decision

The recipient publishes `mailbox.create {ref, exit, sealed}`, signed by a one-time key, with the pickup point and a notify token sealed to the exit hub, and gives the sender a card `MNBOX1.<ref>.<exit>`. The exit hub posts a 'ready' notice on the notify token.

## Rejected

The sender writes the pickup point into the exit slot (the sender learns it); the recipient collects in person at the exit hub (faces).

## Consequences

A mailbox is tied to one exit hub; to use another, the recipient makes another mailbox.
