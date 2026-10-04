// roles.mjs — set up the three roles: hub, recipient (mailbox), sender (tracker).
import { newIdentity, announce, grant, createMailbox } from "./registry.mjs";
import { newLabelKey } from "./label.mjs";
import { newBoxKey, sealTo } from "./seal.mjs";
import { grantContext, mailboxContext } from "./hub.mjs";
import { openReceipt, custodyTopic } from "./receipts.mjs";
import { hex, fromHex, randomBytes } from "./bytes.mjs";

/**
 * Create a hub's keys and its announce event.
 * @param card { name, city, country, policy, intake } — public, no street address
 */
export function createHub(card, { now, labelValidityDays = 90 } = {}) {
  const identity = newIdentity(undefined, now);
  const label = newLabelKey();
  const box = newBoxKey();
  const at = now ? now() : Date.now();
  const labelKeys = [{ epoch: 1, pub: hex(label.pub), notAfter: at + labelValidityDays * 86400000 }];
  const hub = {
    address: identity.address,
    identity,
    labelPrivs: [label.priv],
    boxPriv: box.priv,
    boxPub: box.pub,
    policy: card.policy,
    seen: new Set(),
    card: { ...card, labelKeys, boxPub: hex(box.pub) },
  };
  return { hub, event: announce(identity, hub.card) };
}

/**
 * Hub `to` grants hub `from` the right to ship to it: `to`'s private handoff address,
 * sealed so that ONLY `from` can read it (docs/adr/0004).
 * @param fromHub the folded registry entry of the hub being granted
 */
export function grantAddress(to, fromHub, handoffAddress) {
  const sealed = sealTo(fromHex(fromHub.boxPub), handoffAddress, grantContext(fromHub.address, to.address));
  return grant(to.identity, fromHub.address, sealed);
}

/**
 * A recipient creates a mailbox at an exit hub: pickup instructions sealed to that hub.
 * Returns the registry event plus the card to hand to the sender out of band. The card
 * holds only the ref, never the pickup address (docs/adr/0005).
 */
export function createRecipientMailbox(exitHub, pickup) {
  const notify = randomBytes(16);
  const event = createMailbox(exitHub.address, (ref) =>
    sealTo(fromHex(exitHub.boxPub), { ...pickup, notify: hex(notify) }, mailboxContext(exitHub.address, ref)));
  return { event, card: { mailboxRef: event.payload.ref, exit: exitHub.address }, notifyToken: notify };
}

/**
 * The sender's private tracker: turns custody receipts into a progress timeline.
 * tokens: tracking.custody from planRoute.
 */
export function trackerTopics(tracking) {
  return tracking.custody.map((t, i) => ({ index: i, topic: custodyTopic(t), token: t }));
}

/** Decode whatever receipts arrived into a per-step timeline. */
export function trackProgress(tracking, inbox) {
  const steps = tracking.custody.map((t, i) => {
    const topic = custodyTopic(t);
    const got = inbox.filter((m) => m.topic === topic).map((m) => openReceipt(t, topic, m.bytes)).filter(Boolean);
    return { index: i, receipts: got };
  });
  return steps;
}

