# 0010. Desktop (Basecamp) first, Android next - a recorded exception to ship-both-together

- Status: Accepted
- Date: 2026-10-04

## Context

The app charter says Basecamp and Android ship together. MuleNet's hub flow wants a phone (camera, at the parcel), but the protocol and core had to be proven first.

## Decision

0.1.0 ships the Basecamp core + view (also usable headless as an always-on peer). The Android hub app is the next milestone, on the same packages/core.

## Rejected

Both at once (would have doubled the surface before the protocol was proven).

## Consequences

Until the phone app exists, a hub pastes the label text (scanned with any QR app) into Basecamp.
