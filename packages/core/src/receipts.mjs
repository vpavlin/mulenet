// receipts.mjs — custody receipts over Logos Messaging (docs/adr/0006).
//
// The sender puts two random 16-byte custody tokens into every hop's slot:
//   custodyIn  — shared with the PREVIOUS hop (its custodyOut): "I received it"
//   custodyOut — shared with the NEXT hop (its custodyIn):      "I shipped it"
// Every receipt travels on ONE shared content topic. Each carries a 16-byte tag
// derived from its token, so the holder of the token finds its receipts with one
// comparison, and nobody else can tell which receipts belong together. The body
// is sealed with a key derived from the same token. Nothing about a route is ever
// published in the clear.
//
// Wire: tag(16) || nonce(12) || ChaCha20-Poly1305(key, nonce, aad = tag, json)
//
// Receipts carry DAY granularity only, and hubs post them after a random delay,
// so receipt timing doesn't line up with carrier scan events (docs/SPEC.md §7).
import { chacha20poly1305 } from "@noble/ciphers/chacha.js";
import { hkdf } from "@noble/hashes/hkdf.js";
import { sha256 } from "@noble/hashes/sha2.js";
import { enc, dec, concat, hex, randomBytes, equalBytes } from "./bytes.mjs";

export const RECEIPTS_TOPIC = "/mulenet/1/receipts/proto";
export const RECEIPT_KINDS = ["received", "shipped", "refused", "delivered", "ready", "collected"];

export function custodyTag(token) {
  return sha256(concat(enc.encode("mulenet-custody-tag-v1"), token)).slice(0, 16);
}

function custodyKey(token) {
  return hkdf(sha256, token, enc.encode("mulenet-custody-key-v1"), new Uint8Array(0), 32);
}

/** Build a sealed receipt for a custody token. `body` = { kind, day, ... }. */
export function makeReceipt(token, body, rng = randomBytes) {
  if (!RECEIPT_KINDS.includes(body.kind)) throw new Error("receipt: unknown kind " + body.kind);
  if (body.day && !/^\d{4}-\d{2}-\d{2}$/.test(body.day)) throw new Error("receipt: day must be YYYY-MM-DD");
  const tag = custodyTag(token);
  const nonce = rng(12); // random 96-bit nonce: a token carries a handful of receipts at most
  const pt = enc.encode(JSON.stringify({ v: 1, id: hex(rng(8)), ...body }));
  const ct = chacha20poly1305(custodyKey(token), nonce, tag).encrypt(pt);
  return { topic: RECEIPTS_TOPIC, bytes: concat(tag, nonce, ct) };
}

/** Does this receipt belong to `token`? (cheap tag check, no decryption) */
export function receiptMatches(token, bytes) {
  return bytes.length > 28 && equalBytes(bytes.slice(0, 16), custodyTag(token));
}

/** Open a receipt. Returns null if it isn't for this token or fails to authenticate. */
export function openReceipt(token, bytes) {
  if (!receiptMatches(token, bytes)) return null;
  try {
    const pt = chacha20poly1305(custodyKey(token), bytes.slice(16, 28), bytes.slice(0, 16)).decrypt(bytes.slice(28));
    return JSON.parse(dec.decode(pt));
  } catch {
    return null;
  }
}

/** Truncate a timestamp to the UTC day, the only time resolution receipts carry. */
export function dayOf(ms) {
  return new Date(ms).toISOString().slice(0, 10);
}
