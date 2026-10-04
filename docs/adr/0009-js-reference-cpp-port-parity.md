# 0009. One JS reference implementation, a byte-identical C++ port

- Status: Accepted
- Date: 2026-10-04

## Context

Basecamp cores are C++; the Android app will be React Native. Two implementations diverge silently unless pinned.

## Decision

`packages/core` is the reference (also used by the simulator and later the phone). `mulenet_core` is a C++ port over OpenSSL 3. Golden vectors generated from JS (deterministic randomness) are replayed in C++: labels, seals and receipts must be byte-identical; registry events must verify both ways; a whole JS-built network is walked by C++ hubs.

## Rejected

C++ only via a WASM/JSI bridge on the phone (heavier; harder to debug).

## Consequences

Every wire change lands in both, and `gen-vectors.mjs` regenerates the vectors. Primitives are restricted to what OpenSSL has (XChaCha20 was dropped for ChaCha20-Poly1305).
