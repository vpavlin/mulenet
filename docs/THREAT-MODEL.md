# Threat model

## Goals

1. **Sender-recipient unlinkability.** No single party other than the sender can tell who sent a
   parcel to whom. The recipient may know the sender (a gift) or not.
2. **Protect the hubs as much as possible.** A hub's street or locker address is never public,
   never visible to senders, and visible only to the hubs it chose to receive from. A hub never
   has to open a parcel. A hub has a signed alibi for every handover.

## Non-goals (v1)

- Hiding that MuleNet is used, or who runs a hub (cities and the trust graph are public).
- Hiding the contents from hubs who decide to open a parcel. Nothing technical stops a hub from
  opening a sleeve; the design just never requires it.
- Resistance to a global passive observer of the postal system at today's traffic volumes.

## What each party sees

| | sender | entry hub | middle hub | exit hub | recipient | carrier | public |
|---|---|---|---|---|---|---|---|
| sender's identity | - | maybe (hand-over) | no | no | maybe | maybe (drop-off) | no |
| route (which hubs) | yes | prev/next | prev/next | prev/next | no | one leg | no |
| hub addresses | **no** | next hub's | next hub's | - | no | one leg | no |
| pickup point | **no** | no | no | yes | yes | last leg | no |
| contents | yes | no* | no* | no* | yes | no | no |
| declared category + weight band | yes | yes | yes | yes | - | weight | no |
| receipts | all | its own | its own | its own | ready | - | opaque blobs |

\* unless they open a sleeve.

## Assumptions

- At least one hub on the route is honest (the mixnet assumption).
- Hubs re-box, pad to the band and use the batch days. A lazy hub that forwards the original box
  links its two neighbours.
- Carriers don't collude with every hub on the route.

## Attacks considered

| attack | defence |
|---|---|
| Follow the box across hops (appearance, label) | re-box at every hop; the label is re-printed and shares no bytes with the previous one |
| Count remaining hops from the label size | fixed-size header (Sphinx); every hop sees 691 bytes |
| Photograph the parcel at two non-adjacent hubs | sleeves: the hubs either side of an honest hub never see the same object |
| Weigh to match in and out | pad to the band target; the sleeve weight is absorbed |
| Time in and out | random hold + shared batch days; receipts day-resolution, posted with a delay |
| Re-send a copied label to trace a route | replay tag per hub |
| Swap or tamper the contents | sleeve seal codes checked at every hop; mismatch = refused + reported |
| A sender maps hub addresses | grants are sealed hub-to-hub; senders never get one |
| A hub learns the recipient's address | only the exit hub can open the mailbox; the card holds only a ref |
| Link a recipient's mailbox to their identity | mailboxes are signed by a one-time key |
| Forge a directory entry or a vouch | every event is signed; the fold checks the signature and the steward set |
| Fake receipts | receipts are AEAD under a token only the route's parties know |

## Known weaknesses

- **Anonymity set.** With few parcels a colluding majority links by timing. The simulator's
  Adversary view shows it: one honest hub with no other traffic hides the parcel among 1-4 boxes.
  This only improves with more parcels, longer holds, shared batch days and decoy boxes.
- **The trust graph is public.** Who granted whom is visible; so is each hub's city.
- **Carrier accounts.** Locker and parcel services want a phone number or email: a hub's carrier
  account can identify it to the carrier.
- **Entry and exit faces.** A meetup hand-over shows the sender to the entry hub; a pickup shows
  the recipient to the carrier. Lockers help on both ends.
- **Contents.** See OPEN-PROBLEMS.md: the hub can't verify a declaration without opening.
