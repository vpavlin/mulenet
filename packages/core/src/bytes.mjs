// Small byte helpers shared by every module. No dependencies beyond noble's RNG.
import { randomBytes as nobleRandom } from "@noble/hashes/utils.js";

export const enc = new TextEncoder();
export const dec = new TextDecoder();

export function randomBytes(n) {
  return nobleRandom(n);
}

export function concat(...parts) {
  let len = 0;
  for (const p of parts) len += p.length;
  const out = new Uint8Array(len);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}

export function xor(a, b) {
  if (a.length !== b.length) throw new Error("xor: length mismatch");
  const out = new Uint8Array(a.length);
  for (let i = 0; i < a.length; i++) out[i] = a[i] ^ b[i];
  return out;
}

/** Constant-time equality for MAC checks. */
export function equalBytes(a, b) {
  if (a.length !== b.length) return false;
  let d = 0;
  for (let i = 0; i < a.length; i++) d |= a[i] ^ b[i];
  return d === 0;
}

const HEXC = "0123456789abcdef";
export function hex(b) {
  let s = "";
  for (const x of b) s += HEXC[x >> 4] + HEXC[x & 15];
  return s;
}
export function fromHex(s) {
  if (s.length % 2) throw new Error("fromHex: odd length");
  const a = new Uint8Array(s.length / 2);
  for (let i = 0; i < a.length; i++) {
    const v = parseInt(s.substr(i * 2, 2), 16);
    if (Number.isNaN(v)) throw new Error("fromHex: bad digit");
    a[i] = v;
  }
  return a;
}

const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
export function b64url(bytes) {
  let s = "";
  let i = 0;
  for (; i + 2 < bytes.length; i += 3) {
    const n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    s += B64[n >> 18] + B64[(n >> 12) & 63] + B64[(n >> 6) & 63] + B64[n & 63];
  }
  const rest = bytes.length - i;
  if (rest === 1) {
    const n = bytes[i] << 16;
    s += B64[n >> 18] + B64[(n >> 12) & 63];
  } else if (rest === 2) {
    const n = (bytes[i] << 16) | (bytes[i + 1] << 8);
    s += B64[n >> 18] + B64[(n >> 12) & 63] + B64[(n >> 6) & 63];
  }
  return s;
}
export function fromB64url(s) {
  const out = [];
  let buf = 0, bits = 0;
  for (const c of s) {
    const v = B64.indexOf(c);
    if (v < 0) throw new Error("fromB64url: bad char");
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) { bits -= 8; out.push((buf >> bits) & 255); }
  }
  return Uint8Array.from(out);
}

/** Fixed-width big-endian unsigned writes/reads (u8/u16/u32). */
export function u16(n) { return Uint8Array.of((n >> 8) & 255, n & 255); }
export function readU16(b, o) { return (b[o] << 8) | b[o + 1]; }
export function u32(n) { return Uint8Array.of((n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255); }
export function readU32(b, o) { return ((b[o] << 24) >>> 0) + (b[o + 1] << 16) + (b[o + 2] << 8) + b[o + 3]; }
