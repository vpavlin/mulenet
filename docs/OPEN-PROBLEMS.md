# Open problems

Things we've thought about and don't have an answer for yet. Each needs a decision (and an ADR)
before MuleNet carries anything beyond a test parcel between friends.

## 1. Contents: protecting the hub without opening the parcel

**The tension.** A hub carries a sealed parcel it can't see into. If the parcel holds something
illegal, the hub is the one holding it. The obvious fix, letting a hub inspect the contents,
breaks the basic expectation of sending a parcel: whoever handles it doesn't look inside. It also
weakens unlinkability, because an item two hubs have both seen links them.

**What MuleNet does today (v1): uninspected.**

- Hubs never open anything. They remove one outer sleeve and check its seal code.
- Hubs may **weigh and measure**: the box class, the weight band and the declared net weight are
  checked, and a mismatch is refused.
- The sender **declares a category** in every hop's slot, and each hub publishes which categories
  it carries (`accepts`). A hub can refuse a whole category (e.g. "undeclared") up front.
- A hub can **refuse** any parcel; the refusal is reported to the sender and the previous hub.

**What that doesn't solve.** A declaration is unverifiable without opening, and a refusal after
receipt still leaves the hub holding the box (see 4).

**Ideas considered, none adopted:**

- Inspection at the entry hub only, then a re-seal. It breaks the expectation above, and the
  entry hub is the one most likely to know the sender.
- Non-opening checks: X-ray or other scanners at some hubs, to confirm there's no liquid or
  battery. That's sometimes acceptable, but most hubs won't have one, and it's still a look
  inside.
- A sender bond, slashed when a declaration turns out false. That needs someone to find out it was
  false, which brings us back to inspection.
- Vouched senders, so only people a steward knows can send. That weakens sender anonymity towards
  the steward.

So we don't yet know how to keep the sender private without leaving the hub exposed. Until we do,
MuleNet is for test parcels between people who trust each other.

## 2. Anonymity set at low volume

With a handful of parcels, colluding hubs link sender and recipient by timing. Candidate
mitigations: decoy boxes between hubs, longer and shared batch windows, a minimum-traffic gate
that holds parcels until N others pass through. None implemented.

## 3. Carrier accounts and KYC

Lockers and parcel services require a phone number or email for the receiver. That identifies a
hub (or a recipient) to the carrier. Per-country research is needed: which services allow
pseudonymous accounts, and whether "send to a locker, the code goes to this number" can use a
shared or throwaway contact.

## 4. Refusals and returns

A hub that refuses a parcel holds a box it didn't want. There's no return path: the onion only
goes forward, and a hub doesn't know the previous hub's address (only the carrier's return
label, if any). Options: a reply onion (SURB-style) built by the sender for returns; hold-for-N-
days-then-dispose; send to a designated "lost and found" hub.

## 5. Customs

International parcels need a sender declaration with name and address. Domestic routes (or the
EU, which has no internal customs) work; anything else needs a hub that carries parcels across in
person.

## 6. Label key rotation

Hubs publish one label key valid for a year; old keys stay valid for their grace period. A
compromised label key reveals the routing of every label built for it until it expires. Rotation
cadence and revocation are not designed yet.

## 7. Payments and accountability

Hubs work for free in v1. LEZ could carry per-hop escrow (released when the next hop's
"received" receipt appears), hub bonds and private payments, but a receipt-for-payment scheme
must not make receipts public or link payments across hops.

## 8. Recipient presence at the exit

Even with a locker, the carrier knows who picked up. A pickup by a third party, or a second exit
hop the recipient fetches from in person, would help.
