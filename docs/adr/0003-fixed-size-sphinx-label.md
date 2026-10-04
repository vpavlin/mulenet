# 0003. The label is a fixed-size Sphinx-style onion in a QR code

- Status: Accepted
- Date: 2026-10-04

## Context

A simple layered onion shrinks at every hop, telling each hub how far it is from either end. The label must also work offline: a hub processes a parcel with no network.

## Decision

A Sphinx-style header: 5 slots of 128 bytes, X25519 per hop with a fresh ephemeral key carried in the previous slot (no blinding), HKDF-SHA256 keys, HMAC-SHA256 MAC, ChaCha20 stream, Sphinx filler. 691 bytes at every hop, ~926 chars of base64url, a QR code at error correction M. Replay tags per hub.

## Rejected

Sphinx with group-element blinding (smaller slots, more complex, no benefit at 5 hops); a digital-only onion with a short ID on the box (needs the network at every hop, and the ID itself is a correlator); reusing nim-libp2p-mix's packet directly (built for network payloads, not printable).

## Consequences

Routes are limited to 5 hops. The label holds no human-readable data at all.
