// seal.mjs — anonymous public-key sealing (X25519 + HKDF + ChaCha20-Poly1305).
//
// Used for the two things that must reach exactly one hub and nobody else:
//   - address GRANTS: hub B's handoff address sealed to hub A, so A can ship to B
//     but a sender planning a route through A→B never learns where B is (docs/adr/0004);
//   - MAILBOXES: a recipient's pickup instructions sealed to the exit hub, so the
//     sender never learns where the recipient collects (docs/adr/0005).
// `context` is bound as AAD so a sealed blob can't be lifted into another record.
import { x25519 } from "@noble/curves/ed25519.js";
import { chacha20poly1305 } from "@noble/ciphers/chacha.js";
import { hkdf } from "@noble/hashes/hkdf.js";
import { sha256 } from "@noble/hashes/sha2.js";
import { enc, dec, concat, randomBytes } from "./bytes.mjs";

function key(shared, ephPub, recipientPub) {
  return hkdf(sha256, shared, enc.encode("mulenet-seal-v1"), concat(ephPub, recipientPub), 32);
}

/** Seal `plaintext` (Uint8Array or JSON-able value) to an X25519 public key. */
export function sealTo(recipientPub, plaintext, context) {
  const pt = plaintext instanceof Uint8Array ? plaintext : enc.encode(JSON.stringify(plaintext));
  const eph = randomBytes(32);
  const ephPub = x25519.getPublicKey(eph);
  const k = key(x25519.getSharedSecret(eph, recipientPub), ephPub, recipientPub);
  // Key is unique per blob (fresh ephemeral), so a zero nonce is safe.
  const ct = chacha20poly1305(k, new Uint8Array(12), enc.encode(context)).encrypt(pt);
  return concat(ephPub, ct);
}

/** Open a sealed blob; returns bytes, or parsed JSON when `json` is true. Throws on failure. */
export function openSealed(recipientPriv, blob, context, json = true) {
  const ephPub = blob.slice(0, 32);
  const recipientPub = x25519.getPublicKey(recipientPriv);
  const k = key(x25519.getSharedSecret(recipientPriv, ephPub), ephPub, recipientPub);
  const pt = chacha20poly1305(k, new Uint8Array(12), enc.encode(context)).decrypt(blob.slice(32));
  return json ? JSON.parse(dec.decode(pt)) : pt;
}

export function newBoxKey() {
  const priv = randomBytes(32);
  return { priv, pub: x25519.getPublicKey(priv) };
}
