// Generate golden vectors for the C++ core (mulenet_core/test/parity_test.cpp).
// Deterministic: every random byte comes from a sha256 counter stream, so the C++
// side can replay the exact same randomness and must produce identical bytes.
//   node packages/core/test/gen-vectors.mjs > mulenet_core/test/vectors.json
import { sha256 } from "@noble/hashes/sha2.js";
import { x25519 } from "@noble/curves/ed25519.js";
import { buildLabel, peelLabel, HOP_RELAY, HOP_EXIT, sealTo, makeReceipt, sleeveCodeText, newIdentity, makeEvent } from "../src/index.mjs";
import { hex, enc } from "../src/bytes.mjs";

function stream(seed) {
  let ctr = 0;
  return (n) => {
    const out = new Uint8Array(n);
    for (let o = 0; o < n; o += 32) out.set(sha256(enc.encode(`${seed}|${ctr++}`)).slice(0, Math.min(32, n - o)), o);
    return out;
  };
}

const keyRng = stream("keys");
const labels = [];
for (const n of [1, 3, 5]) {
  const privs = Array.from({ length: n }, () => keyRng(32));
  const fill = stream(`routing-${n}`);
  const hops = privs.map((p, i) => ({
    labelPub: x25519.getPublicKey(p),
    routing: {
      type: i === n - 1 ? HOP_EXIT : HOP_RELAY, holdMin: i, holdMax: i + 3, boxClass: 1 + (i % 3), weightClass: 2,
      category: i === 0 ? 99 : 1, netGrams: 400 - 20 * i,
      sleeveCode: fill(8), custodyIn: fill(16), custodyOut: fill(16), ref: fill(16),
    },
  }));
  const seed = `label-${n}`;
  const { header } = buildLabel(hops, stream(seed));
  const peels = [];
  let h = header;
  for (let i = 0; i < n; i++) {
    const r = peelLabel(h, privs[i]);
    peels.push({ next: r.next ? hex(r.next.header) : null, replayTag: hex(r.replayTag) });
    if (r.next) h = r.next.header;
  }
  labels.push({
    seed,
    privs: privs.map(hex),
    hops: hops.map((x) => ({ labelPub: hex(x.labelPub), routing: Object.fromEntries(Object.entries(x.routing).map(([k, v]) => [k, v instanceof Uint8Array ? hex(v) : v])) })),
    header: hex(header),
    peels,
  });
}

const boxPriv = keyRng(32);
const seal = { recipientPriv: hex(boxPriv), recipientPub: hex(x25519.getPublicKey(boxPriv)), context: "mulenet-grant-v1|0xaa|0xbb", plaintext: '{"locker":"PRG-7","name":"hub"}', seed: "seal" };
seal.blob = hex(sealTo(x25519.getPublicKey(boxPriv), enc.encode(seal.plaintext), seal.context, stream(seal.seed)));

const token = keyRng(16);
const receipt = { token: hex(token), seed: "receipt", body: { kind: "shipped", day: "2026-10-08" } };
receipt.bytes = hex(makeReceipt(token, receipt.body, stream(receipt.seed)).bytes);

const sleeve = { bytes: hex(keyRng(8)) };
sleeve.text = sleeveCodeText(Uint8Array.from(sleeve.bytes.match(/../g).map((x) => parseInt(x, 16))));

// A signed registry event, for C++ verify parity (ECDSA signatures are not deterministic
// across libraries, so only verification is compared).
const id = newIdentity(keyRng(32), () => 1790000000000);
const event = makeEvent(id, "hub.announce", { name: "Prague mule", city: "Prague", country: "CZ", policy: { accepts: ["*"], maxWeightClass: 3 }, labelKeys: [] });

console.log(JSON.stringify({ labels, seal, receipt, sleeve, event }, null, 1));
