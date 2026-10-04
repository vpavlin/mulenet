// label.mjs — the MuleNet shipping label: a fixed-size, Sphinx-style onion header.
//
// Each hop scans the label QR, peels exactly one layer with its X25519 label key,
// learns ONLY its own routing slot (where to ship next, how long to hold, which
// sleeve to remove, which custody tokens to use), and prints the remaining header
// on the fresh outer box. The header is the SAME SIZE at every hop, so a hop can't
// tell how far it is from the sender or the recipient (docs/adr/0003).
//
// Construction follows Sphinx (Danezis & Goldberg 2009) with two simplifications:
//   - each hop's ephemeral key is a FRESH X25519 key carried, encrypted, in the
//     previous hop's slot (no group-element blinding; same unlinkability, larger slot);
//   - there is no payload: the "payload" is the physical parcel.
import { x25519 } from "@noble/curves/ed25519.js";
import { chacha20 } from "@noble/ciphers/chacha.js";
import { hkdf } from "@noble/hashes/hkdf.js";
import { hmac } from "@noble/hashes/hmac.js";
import { sha256 } from "@noble/hashes/sha2.js";
import {
  enc, concat, xor, equalBytes, randomBytes, b64url, fromB64url, u16, readU16,
} from "./bytes.mjs";

export const MAGIC = enc.encode("MN");
export const VERSION = 1;
export const MAX_HOPS = 5;
export const ROUTING_SIZE = 80;
const KEY = 32; // X25519 public key
const MAC = 16;
export const SLOT_SIZE = KEY + MAC + ROUTING_SIZE; // 128
export const BETA_SIZE = MAX_HOPS * SLOT_SIZE; // 640
export const HEADER_SIZE = MAGIC.length + 1 + KEY + MAC + BETA_SIZE; // 691
export const TEXT_PREFIX = "MN1.";

export const HOP_RELAY = 1;
export const HOP_EXIT = 2;

// ── routing slot ─────────────────────────────────────────────────────────────
// Fixed 80-byte layout (docs/SPEC.md §3.2). Every field is fixed width so the slot
// size never depends on content.
//   0  type        u8   1 = relay, 2 = exit
//   1  flags       u8   reserved (0)
//   2  holdMin     u8   days to hold before shipping (mixing delay), min
//   3  holdMax     u8   … max; the hub picks uniformly in [min,max] then ships in its next batch
//   4  boxClass    u8   outer box class to ship in (docs/SPEC.md §5)
//   5  weightClass u8   gross weight band to pad to
//   6  category    u16  sender's declared content category (docs/SPEC.md §6)
//   8  netGrams    u16  sender's declared net weight of what this hop receives, grams
//  10  sleeveCode  8B   the tamper-seal code on the sleeve THIS hop removes
//  18  custodyIn   16B  token for "I received it" (shared with the previous hop + sender)
//  34  custodyOut  16B  token for "I shipped it" (shared with the next hop + sender)
//  50  ref         16B  relay: the next hop's address-grant ref; exit: the mailbox ref
//  66  reserved    14B  zeros
export function encodeRouting(r) {
  const out = new Uint8Array(ROUTING_SIZE);
  out[0] = r.type;
  out[1] = 0;
  out[2] = r.holdMin ?? 0;
  out[3] = r.holdMax ?? r.holdMin ?? 0;
  out[4] = r.boxClass ?? 0;
  out[5] = r.weightClass ?? 0;
  out.set(u16(r.category ?? 0), 6);
  out.set(u16(r.netGrams ?? 0), 8);
  out.set(fixed(r.sleeveCode, 8, "sleeveCode"), 10);
  out.set(fixed(r.custodyIn, 16, "custodyIn"), 18);
  out.set(fixed(r.custodyOut, 16, "custodyOut"), 34);
  out.set(fixed(r.ref, 16, "ref"), 50);
  return out;
}

export function decodeRouting(b) {
  if (b.length !== ROUTING_SIZE) throw new Error("routing: wrong size");
  const type = b[0];
  if (type !== HOP_RELAY && type !== HOP_EXIT) throw new Error("routing: unknown hop type " + type);
  return {
    type,
    holdMin: b[2],
    holdMax: b[3],
    boxClass: b[4],
    weightClass: b[5],
    category: readU16(b, 6),
    netGrams: readU16(b, 8),
    sleeveCode: b.slice(10, 18),
    custodyIn: b.slice(18, 34),
    custodyOut: b.slice(34, 50),
    ref: b.slice(50, 66),
  };
}

function fixed(v, n, name) {
  if (!(v instanceof Uint8Array) || v.length !== n) throw new Error(`routing: ${name} must be ${n} bytes`);
  return v;
}

// ── per-hop keys ─────────────────────────────────────────────────────────────
function hopKeys(shared, alpha) {
  const okm = hkdf(sha256, shared, enc.encode("mulenet-label-v1"), alpha, 64);
  return { mac: okm.slice(0, 32), stream: okm.slice(32, 64) };
}

function stream(key, len) {
  // Each stream key is used for exactly one header (fresh ephemeral per hop), so a
  // fixed zero nonce is safe.
  return chacha20(key, new Uint8Array(12), new Uint8Array(len));
}

function mac(key, alpha, beta) {
  return hmac(sha256, key, concat(MAGIC, Uint8Array.of(VERSION), alpha, beta)).slice(0, MAC);
}

// ── sender: build a label ────────────────────────────────────────────────────
/**
 * Build the label for a route.
 * @param hops  [{ labelPub: Uint8Array(32), routing: {...} }] in travel order; the
 *              last hop's routing.type must be HOP_EXIT, all others HOP_RELAY.
 * @param rng   injectable for test vectors; defaults to the CSPRNG.
 * @returns { header: Uint8Array(HEADER_SIZE), text: string }
 */
export function buildLabel(hops, rng = randomBytes) {
  const n = hops.length;
  if (n < 1 || n > MAX_HOPS) throw new Error(`label: route must have 1..${MAX_HOPS} hops`);
  hops.forEach((h, i) => {
    const want = i === n - 1 ? HOP_EXIT : HOP_RELAY;
    if (h.routing.type !== want) throw new Error(`label: hop ${i} must be ${want === HOP_EXIT ? "exit" : "relay"}`);
  });

  const eph = hops.map(() => rng(32));
  const alphas = eph.map((e) => x25519.getPublicKey(e));
  const keys = hops.map((h, i) => hopKeys(x25519.getSharedSecret(eph[i], h.labelPub), alphas[i]));
  const streams = keys.map((k) => stream(k.stream, BETA_SIZE + SLOT_SIZE));

  // Filler: the bytes the last hop's beta must end in so every earlier hop's
  // shift-and-decrypt lands exactly on them.
  let filler = new Uint8Array(0);
  for (let i = 1; i < n; i++) {
    const s = streams[i - 1];
    filler = xor(concat(filler, new Uint8Array(SLOT_SIZE)), s.slice(BETA_SIZE + SLOT_SIZE - i * SLOT_SIZE));
  }

  // Innermost (exit) layer.
  const exitSlot = concat(new Uint8Array(KEY), new Uint8Array(MAC), encodeRouting(hops[n - 1].routing));
  const pad = rng((MAX_HOPS - n) * SLOT_SIZE);
  const headLen = (MAX_HOPS - n + 1) * SLOT_SIZE;
  let beta = concat(xor(concat(exitSlot, pad), streams[n - 1].slice(0, headLen)), filler);
  let gamma = mac(keys[n - 1].mac, alphas[n - 1], beta);

  // Wrap outward.
  for (let i = n - 2; i >= 0; i--) {
    const plain = concat(alphas[i + 1], gamma, encodeRouting(hops[i].routing), beta.slice(0, BETA_SIZE - SLOT_SIZE));
    beta = xor(plain, streams[i].slice(0, BETA_SIZE));
    gamma = mac(keys[i].mac, alphas[i], beta);
  }

  const header = concat(MAGIC, Uint8Array.of(VERSION), alphas[0], gamma, beta);
  return { header, text: labelToText(header) };
}

// ── hop: peel one layer ──────────────────────────────────────────────────────
/**
 * Peel this hop's layer.
 * @param header      Uint8Array(HEADER_SIZE) or the label text
 * @param labelPriv   the hop's X25519 label private key (try each live key epoch)
 * @returns { routing, replayTag, next: {header,text} | null }
 * @throws on a malformed label or a MAC failure (wrong key / tampered label)
 */
export function peelLabel(header, labelPriv) {
  const h = typeof header === "string" ? labelFromText(header) : header;
  if (h.length !== HEADER_SIZE) throw new Error("label: wrong size");
  if (!equalBytes(h.slice(0, 2), MAGIC) || h[2] !== VERSION) throw new Error("label: not a MuleNet v1 label");
  let o = 3;
  const alpha = h.slice(o, (o += KEY));
  const gamma = h.slice(o, (o += MAC));
  const beta = h.slice(o, (o += BETA_SIZE));

  const shared = x25519.getSharedSecret(labelPriv, alpha);
  const k = hopKeys(shared, alpha);
  if (!equalBytes(mac(k.mac, alpha, beta), gamma)) throw new Error("label: MAC mismatch (not for this hub, or tampered)");

  const b = xor(concat(beta, new Uint8Array(SLOT_SIZE)), stream(k.stream, BETA_SIZE + SLOT_SIZE));
  const nextAlpha = b.slice(0, KEY);
  const nextGamma = b.slice(KEY, KEY + MAC);
  const routing = decodeRouting(b.slice(KEY + MAC, SLOT_SIZE));
  const nextBeta = b.slice(SLOT_SIZE);
  // Replay protection: a hop refuses a label it has already processed (a copied label
  // re-sent to trace the route). The tag is derived from the shared secret, so it is
  // unique per (label, hop) and reveals nothing to anyone else.
  const replayTag = sha256(concat(enc.encode("mulenet-replay-v1"), shared));

  if (routing.type === HOP_EXIT) return { routing, replayTag, next: null };
  const next = concat(MAGIC, Uint8Array.of(VERSION), nextAlpha, nextGamma, nextBeta);
  return { routing, replayTag, next: { header: next, text: labelToText(next) } };
}

/** Try every live label key of a hub (key rotation keeps old epochs for a grace period). */
export function peelWithAnyKey(header, labelPrivs) {
  let last;
  for (const priv of labelPrivs) {
    try { return peelLabel(header, priv); } catch (e) { last = e; }
  }
  throw last || new Error("label: no label keys");
}

export function labelToText(header) { return TEXT_PREFIX + b64url(header); }
export function labelFromText(text) {
  const t = text.trim();
  if (!t.startsWith(TEXT_PREFIX)) throw new Error("label: not a MuleNet label");
  return fromB64url(t.slice(TEXT_PREFIX.length));
}

export function newLabelKey() {
  const priv = randomBytes(32);
  return { priv, pub: x25519.getPublicKey(priv) };
}
