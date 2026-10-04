// receipts.mjs — custody receipts over Logos Messaging (docs/adr/0006).
//
// The sender puts two random 16-byte custody tokens into every hop's slot:
//   custodyIn  — shared with the PREVIOUS hop (its custodyOut): "I received it"
//   custodyOut — shared with the NEXT hop (its custodyIn):      "I shipped it"
// A token derives both a content topic and an AEAD key, so only the parties that
// hold the token (the two neighbours and the sender) can find or read a receipt.
// Nothing about the route is ever published in the clear.
//
// Receipts carry DAY granularity only, and hubs post them after a random delay,
// so receipt timing doesn't line up with carrier scan events (docs/SPEC.md §7).
import { xchacha20poly1305 } from "@noble/ciphers/chacha.js";
import { hkdf } from "@noble/hashes/hkdf.js";
import { sha256 } from "@noble/hashes/sha2.js";
import { enc, dec, concat, hex, randomBytes } from "./bytes.mjs";

export const RECEIPT_KINDS = ["received", "shipped", "refused", "delivered", "ready", "collected"];

export function custodyTopic(token) {
  const h = sha256(concat(enc.encode("mulenet-custody-topic-v1"), token));
  return `/mulenet/1/c-${hex(h).slice(0, 32)}/proto`;
}

function custodyKey(token) {
  return hkdf(sha256, token, enc.encode("mulenet-custody-key-v1"), new Uint8Array(0), 32);
}

/** Build a sealed receipt for a custody token. `body` = { kind, day, ... }. */
export function makeReceipt(token, body) {
  if (!RECEIPT_KINDS.includes(body.kind)) throw new Error("receipt: unknown kind " + body.kind);
  if (body.day && !/^\d{4}-\d{2}-\d{2}$/.test(body.day)) throw new Error("receipt: day must be YYYY-MM-DD");
  const topic = custodyTopic(token);
  const nonce = randomBytes(24);
  const pt = enc.encode(JSON.stringify({ v: 1, id: hex(randomBytes(8)), ...body }));
  const ct = xchacha20poly1305(custodyKey(token), nonce, enc.encode(topic)).encrypt(pt);
  return { topic, bytes: concat(nonce, ct) };
}

/** Open a receipt received on `custodyTopic(token)`. Returns null if it isn't ours. */
export function openReceipt(token, topic, bytes) {
  try {
    const pt = xchacha20poly1305(custodyKey(token), bytes.slice(0, 24), enc.encode(topic)).decrypt(bytes.slice(24));
    return JSON.parse(dec.decode(pt));
  } catch {
    return null;
  }
}

/** Truncate a timestamp to the UTC day, the only time resolution receipts carry. */
export function dayOf(ms) {
  return new Date(ms).toISOString().slice(0, 10);
}
