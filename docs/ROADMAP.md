# Roadmap

## Done (0.1.0)

- Onion label (fixed-size Sphinx-style), sleeves, box classes, weight bands, categories.
- Signed hub directory over Loam with RBSR catch-up; vouching; trust roots.
- Sealed hub-to-hub address grants; recipient mailboxes; custody receipts with backfill.
- JS reference + C++ core with byte parity; Basecamp core + view; simulator; render harness.
- Verified on the live Logos test fleet (two headless peers + Basecamp 0.2.0).

## Next

1. **Android hub app** (charter: Basecamp and Android together). Camera QR scan, the arrival flow,
   label printing via the system print dialog, receipts over the shared Loam node. Reuses
   `packages/core` directly.
2. **First real parcel**: a 3D-printed item through 3-4 friends. Fix whatever the first run finds.
3. **Basecamp 0.3.0** check (multi-instance core, events) and the multiplatform packages.
4. **Publish**: LAN/vpavlin Basecamp repo + F-Droid.

## Later

- Decoy boxes and minimum-traffic batching (OPEN-PROBLEMS 2).
- Return path / reply onions (OPEN-PROBLEMS 4).
- Label key rotation and revocation (OPEN-PROBLEMS 6).
- LEZ: per-hop escrow, hub bonds, private payments; ZK hub membership (parked LEZ research).
- Seeding the directory from Logos Circles (logos.co/active-circles) as steward candidates.
