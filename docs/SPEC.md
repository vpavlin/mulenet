# MuleNet protocol specification (v1)

Status: implemented in `packages/core` (JS reference) and `mulenet_core` (C++, byte-identical,
pinned by `mulenet_core/test/parity_test.cpp`). Decisions and their reasons: `docs/adr/`.

## 1. Roles

| role | does | learns |
|---|---|---|
| **sender** | plans the route, packs the parcel, hands it to the entry hub | the route (which hubs), never any hub's address, never the recipient's pickup point |
| **hub** | receives, checks the seal + weight, removes one sleeve, re-boxes, ships on | the previous hub (from the carrier), the next hub and its private handoff address, the declared category and weight band |
| **exit hub** | like a hub, but ships to the recipient's pickup point | the pickup point (sealed to it by the recipient) |
| **recipient** | creates a mailbox, collects from the pickup point | nothing about the route |
| **steward** | vouches for hubs (a Logos Circle steward, or you) | public directory only |

## 2. Physical model

- **Core**: the item in its own plain inner packaging.
- **Sleeves**: one per hop, outermost first. Sleeve *i* carries a tamper-evident seal with an
  8-byte code (printed as 13-character Crockford base32, `XXXX-XXXX-XXXXX`). Hop *i* removes
  sleeve *i* and checks its code against its label slot.
- **Box classes** (outer box, discarded at every hop): 1 = S 25x18x8 cm, 2 = M 35x25x15 cm,
  3 = L 50x35x20 cm.
- **Weight bands**: 1 <= 500 g (target 450), 2 <= 1000 g (900), 3 <= 2000 g (1800),
  4 <= 5000 g (4500). Each hub pads the outgoing box to the band's target +- 25 g.
- **Categories** (u16): 1 printed-object, 2 paper, 3 textiles, 4 electronics-no-battery,
  5 electronics-with-battery, 6 hardware-tools, 7 sealed-food, 99 undeclared.

Why sleeves: hop *i-1* exposes sleeve *i* and hop *i* removes it, so the hub before and the hub
after an honest hub never handle the same object (ADR 0002).

## 3. The label

### 3.1 Header

Fixed size at every hop, 691 bytes; text form `MN1.` + base64url (no padding), ~926 chars,
printed as a QR code (error correction M).

```
"MN" (2) | version = 1 (1) | alpha (32) | gamma (16) | beta (640)
```

- `MAX_HOPS` = 5, slot = 128 bytes = next alpha (32) | next gamma (16) | routing (80).
- Per hop: `shared = X25519(hubLabelPriv, alpha)`;
  `okm = HKDF-SHA256(ikm = shared, salt = "mulenet-label-v1", info = alpha, 64)`;
  `kMac = okm[0..32)`, `kStream = okm[32..64)`.
- `gamma = HMAC-SHA256(kMac, "MN" | version | alpha | beta)[0..16)`.
- Peel: verify gamma; `B = (beta | 0^128) XOR ChaCha20(kStream, nonce = 0^12, counter 0)`;
  slot = `B[0..128)`, next beta = `B[128..768)`; next header = `"MN" | 1 | slot.alpha | slot.gamma | next beta`.
- Each hop's ephemeral key is fresh (no blinding); the sender computes the Sphinx filler so every
  MAC verifies after the shift. Construction: `buildLabel` in `packages/core/src/label.mjs`.
- **Replay tag**: `SHA-256("mulenet-replay-v1" | shared)`; a hub refuses a label whose tag it has seen.

### 3.2 Routing slot (80 bytes)

| off | size | field | |
|---|---|---|---|
| 0 | 1 | type | 1 relay, 2 exit |
| 1 | 1 | flags | 0 |
| 2 | 1 | holdMin | days |
| 3 | 1 | holdMax | days (hub picks uniformly, then waits for its next batch day) |
| 4 | 1 | boxClass | outer box to ship in |
| 5 | 1 | weightClass | band to pad to |
| 6 | 2 | category | sender's declaration (big-endian) |
| 8 | 2 | netGrams | declared weight of what this hop receives (core + remaining sleeves) |
| 10 | 8 | sleeveCode | the seal this hop removes |
| 18 | 16 | custodyIn | receipt token shared with the previous hop |
| 34 | 16 | custodyOut | receipt token shared with the next hop |
| 50 | 16 | ref | relay: the next hop's grant ref; exit: the mailbox ref |
| 66 | 14 | reserved | zero |

## 4. The hub directory

A signed append-only event log (loam-sync envelope `{v,id,type,hlc,dev,payload,pub,sig}`,
secp256k1 ECDSA low-S, domain `mulenet`), merged by id, HLC-ordered, synced by RBSR catch-up
on `/mulenet/1/registry/proto`. The fold ignores unsigned or badly signed events.

| type | author | payload |
|---|---|---|
| `hub.announce` | hub | `{name, city, country, policy, intake, labelKeys:[{epoch,pub,notAfter}], boxPub}` (LWW per hub) |
| `hub.retire` | hub | `{}` (tombstone, final) |
| `hub.grant` | hub B | `{to: A, ref, sealed}`: A may ship to B; `sealed` = B's handoff address sealed to A's box key |
| `hub.revoke` | hub B | `{ref}` |
| `steward.add` | steward | `{steward}` (fixpoint from the reader's trust roots) |
| `vouch` / `unvouch` | steward | `{hub}` (latest wins) |
| `mailbox.create` | one-time key | `{ref, exit, sealed}`: pickup instructions sealed to the exit hub |

Tombstones are applied after the pass, so they commute with late originals. A grant whose
author is no longer listed, or a mailbox whose exit is gone, drops out.

**Policy** (in the announce): `accepts` (category ids or `"*"`), `boxClasses`, `maxWeightClass`,
`shipsTo` (country codes or `"*"`), `batchDays` (UTC weekdays, 0 = Sunday), `holdMaxDays`.
**Intake**: `{kind: "meetup" | "locker" | "none", text}` - a public hand-over point, never an address.

**Trust**: each user has trust roots (themselves by default). A hub is eligible when at least one
of the reader's stewards vouches for it.

## 5. Sealing (grants and mailboxes)

`blob = ephPub (32) | ChaCha20-Poly1305(k, nonce = 0^12, aad = context, plaintext)`,
`k = HKDF-SHA256(ikm = X25519(eph, recipientPub), salt = "mulenet-seal-v1", info = ephPub | recipientPub, 32)`.

Contexts: grant `mulenet-grant-v1|<from>|<to>`, mailbox `mulenet-mailbox-v1|<exit>|<ref>`.
Mailbox plaintext carries `notify` (16-byte hex token) for the recipient's "ready" notice.

## 6. Custody receipts

All receipts ride one topic, `/mulenet/1/receipts/proto`:

```
tag (16) | nonce (12) | ChaCha20-Poly1305(key, nonce, aad = tag, json)
tag = SHA-256("mulenet-custody-tag-v1" | token)[0..16)
key = HKDF-SHA256(ikm = token, salt = "mulenet-custody-key-v1", info = "", 32)
json = {"v":1, "id":<8B hex>, "kind":..., "day":"YYYY-MM-DD", ...}
```

Kinds: `received` (on custodyIn), `refused` (custodyIn, with `reason`), `shipped` (custodyOut,
posted when the hub confirms the handover), `delivered` (exit, custodyOut), `ready` (exit, on the
mailbox's notify token), `collected` (reserved).

Receipts carry day resolution only and are posted after a random 2-30 h delay. On the wire each
receipt is an unsigned event `{id = hex(SHA-256(bytes))[0..16), type:"receipt", hlc.wall = UTC day
start, payload:{b: base64url(bytes)}}`, so RBSR backfills receipts for a peer that was offline;
peers drop receipts older than 90 days.

## 7. Planning (sender)

`planRoute(directory, parcel, {entry, mailboxRef, hops, holdDays})`:
the entry hub (chosen by the sender) and the exit hub (the mailbox's) must be vouched and accept
the parcel; middle hubs are chosen uniformly at random among vouched hubs that accept the parcel,
are in distinct cities, and where every consecutive pair has a grant and `shipsTo` covers the next
country. A fresh custody token is drawn per step (hops + 1). The sender keeps the route and tokens
on-device only.

## 8. Arrival (hub)

`processArrival(hub, directory, scan)`: peel with any live label key -> refuse on replay ->
refuse a category outside policy -> refuse a seal code mismatch -> refuse overweight for the band
-> post `received` -> choose hold days, then the next batch day -> padding = band target -
(netGrams - sleeve) - box weight -> relay: open the grant for the next hub; exit: open the mailbox.
`shipped` / `delivered` / `ready` are posted when the hub marks the box handed over.

## 9. Module API (mulenet_core)

All methods return JSON strings (`{"ok":true,...}` or `{"ok":false,"error":"..."}`), at most 4
string args: `snapshot`, `resync`, `setupHub(cardJson)`, `retireHub`, `grantAddress(hub, addressJson)`,
`revokeGrant(ref)`, `scanArrival(scanJson)`, `markShipped(jobId)`, `dismissJob(jobId)`,
`addTrustRoot(addr)`, `removeTrustRoot(addr)`, `vouch(hub)`, `unvouch(hub)`,
`createMailbox(exitHub, pickupJson)`, `planParcel(parcelJson)`, `forgetParcel(id)`,
`labelQr(text)`, `exportLabel(text, name)`. Event: `stateChanged(snapshotJson)`.

Local state (`~/.mulenet-core` or `$MULENET_CORE_DATA`): identity, settings (trust roots),
registry, receipts + outbox, hub keys + replay tags, parcels (route + tokens), jobs, mailboxes.

Transport: `loam_core` (`setSenderId`, `start`, `join`, `sendSealed`, `received`,
`statusChanged`). Frames are JSON, base64 once; receive peels up to three base64 layers.
