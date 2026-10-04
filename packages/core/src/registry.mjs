// registry.mjs — the hub directory as a signed, append-only event log (docs/adr/0007).
//
// Transport-agnostic: events are loam-sync envelopes, merged with mergeEvents and
// reconciled with RBSR like every other Loam app. Every event is signed by its author
// (secp256k1, loam-sync signing, domain "mulenet"); the fold ignores anything unsigned
// or badly signed. State is a pure fold; nothing is ever mutated in place.
//
// Event types (payload shapes in docs/SPEC.md §4):
//   hub.announce   a hub's public card: city, label keys, box key, policy, intake. LWW per hub.
//   hub.retire     tombstone: the hub is gone (final).
//   hub.grant      "hub <to> may ship to me": my address sealed to <to>'s box key.
//   hub.revoke     tombstone for a grant ref.
//   steward.add    a trust root (or an existing steward) appoints a steward.
//   vouch          a steward vouches for a hub; unvouch withdraws it.
//   mailbox.create a recipient's pickup instructions sealed to an exit hub (one-time key).
import { mergeEvents, signEvent, verifyEvent, SoftwareSigner, Clock, address } from "../../../vendor/loam-sync/dist/index.js";
import { hex, fromHex, b64url, randomBytes } from "./bytes.mjs";

export const DOMAIN = "mulenet";
export const EVENT_TYPES = ["hub.announce", "hub.retire", "hub.grant", "hub.revoke", "steward.add", "vouch", "unvouch", "mailbox.create"];

/** A signing identity (hub, steward or a one-time mailbox key). */
export function newIdentity(priv = randomBytes(32), now) {
  const signer = new SoftwareSigner(priv);
  const addr = address(signer.publicKey());
  return { priv, signer, address: addr, clock: new Clock(addr, now) };
}

/** Author and sign one registry event. */
export function makeEvent(identity, type, payload) {
  if (!EVENT_TYPES.includes(type)) throw new Error("registry: unknown event type " + type);
  const ev = { v: 1, id: hex(randomBytes(16)), type, hlc: identity.clock.send(), dev: identity.address, payload };
  return signEvent(identity.signer, DOMAIN, ev);
}

export const announce = (id, card) => makeEvent(id, "hub.announce", card);
export const retire = (id) => makeEvent(id, "hub.retire", {});
export const vouch = (steward, hub) => makeEvent(steward, "vouch", { hub });
export const unvouch = (steward, hub) => makeEvent(steward, "unvouch", { hub });
export const addSteward = (by, steward) => makeEvent(by, "steward.add", { steward });
export const revokeGrant = (id, ref) => makeEvent(id, "hub.revoke", { ref });

export function grant(id, toHub, sealedAddress) {
  return makeEvent(id, "hub.grant", { to: toHub, ref: hex(randomBytes(16)), sealed: b64url(sealedAddress) });
}

/** `sealPickup(ref)` returns the pickup instructions sealed to the exit hub, bound to `ref`. */
export function createMailbox(exitHub, sealPickup) {
  const oneTime = newIdentity();
  const ref = hex(randomBytes(16));
  return makeEvent(oneTime, "mailbox.create", { ref, exit: exitHub, sealed: b64url(sealPickup(ref)) });
}

/** Merge any number of logs (dedup by id, HLC order). */
export function merge(...logs) {
  return mergeEvents(...logs);
}

/**
 * Fold the merged log into the directory.
 * @param log    events (any order; merged here)
 * @param roots  trust-root addresses (stewards by configuration)
 */
export function foldRegistry(log, roots = []) {
  const events = mergeEvents(log).filter((e) => verifyEvent(DOMAIN, e));
  const stewards = new Set(roots);
  const hubs = new Map();
  const grants = new Map();
  const mailboxes = new Map();
  const vouches = new Map(); // `${steward}|${hub}` -> latest vouch/unvouch
  const retired = new Set();
  const revoked = new Set();

  // Stewards first, to a fixpoint: an appointment counts once its author is a steward,
  // whatever order the events arrived in.
  let grew = true;
  while (grew) {
    grew = false;
    for (const e of events) {
      if (e.type === "steward.add" && stewards.has(e.dev) && !stewards.has(e.payload?.steward)) {
        stewards.add(e.payload.steward);
        grew = true;
      }
    }
  }

  for (const e of events) {
    const p = e.payload || {};
    switch (e.type) {
      case "hub.announce":
        hubs.set(e.dev, { ...sanitizeCard(p), address: e.dev, pub: e.pub, updated: e.hlc.wall }); // LWW: log is HLC-ordered
        break;
      case "hub.retire":
        retired.add(e.dev);
        break;
      case "hub.grant":
        if (typeof p.ref === "string" && !grants.has(p.ref)) grants.set(p.ref, { ref: p.ref, from: p.to, to: e.dev, sealed: p.sealed });
        break;
      case "hub.revoke":
        revoked.add(`${e.dev}|${p.ref}`);
        break;
      case "vouch":
      case "unvouch":
        if (stewards.has(e.dev)) vouches.set(`${e.dev}|${p.hub}`, e.type === "vouch");
        break;
      case "mailbox.create":
        if (typeof p.ref === "string" && !mailboxes.has(p.ref)) mailboxes.set(p.ref, { ref: p.ref, exit: p.exit, sealed: p.sealed });
        break;
    }
  }

  // Tombstones apply last, so they commute with a late-arriving original.
  for (const a of retired) hubs.delete(a);
  for (const [ref, g] of grants) if (revoked.has(`${g.to}|${ref}`) || !hubs.has(g.to)) grants.delete(ref);
  for (const [ref, m] of mailboxes) if (!hubs.has(m.exit)) mailboxes.delete(ref);
  for (const h of hubs.values()) {
    h.vouchedBy = [...vouches].filter(([k, on]) => on && k.endsWith("|" + h.address)).map(([k]) => k.split("|")[0]);
  }
  return { hubs, grants, mailboxes, stewards };
}

function sanitizeCard(p) {
  return {
    name: String(p.name || ""),
    city: String(p.city || ""),
    country: String(p.country || ""),
    labelKeys: Array.isArray(p.labelKeys) ? p.labelKeys.filter((k) => typeof k.pub === "string" && k.pub.length === 64) : [],
    boxPub: typeof p.boxPub === "string" ? p.boxPub : "",
    policy: p.policy || {},
    intake: p.intake || { kind: "none" },
  };
}

/** The grant that lets hub `from` ship to hub `to`, if one exists. */
export function grantFor(state, from, to) {
  for (const g of state.grants.values()) if (g.from === from && g.to === to) return g;
  return null;
}

/** The newest label key of a hub that's valid at `atMs`. */
export function liveLabelKey(hub, atMs) {
  const live = hub.labelKeys.filter((k) => !k.notAfter || k.notAfter > atMs).sort((a, b) => b.epoch - a.epoch);
  return live[0] ? fromHex(live[0].pub) : null;
}
