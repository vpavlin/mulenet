# Changelog

## 0.1.0 - 2026-10-04

First working prototype.

- Onion shipping label: fixed-size (691 B) Sphinx-style header, 5 hops max, printed as a QR code.
- Physical onion: one tamper-sealed sleeve per hop, re-box and pad to a weight band at every hop.
- Hub directory as a signed event log over Loam (loam-sync, RBSR catch-up); trust roots + vouching.
- Hub addresses granted hub-to-hub, sealed: senders never see a hub's address.
- Recipient mailboxes: the pickup point is sealed to the exit hub; the sender gets only a card.
- Custody receipts on one topic, token-tagged and sealed, backfilled by RBSR.
- `mulenet_core` (Basecamp core, C++) byte-identical to the JS reference; `mulenet` view (pure QML).
- Simulator with a dev visualization; real-core QML render harness; headless driver (hub/mn.py).
- Verified on the live Logos test fleet: two headless peers and Basecamp 0.2.0.
