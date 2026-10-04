import test from "node:test";
import assert from "node:assert/strict";
import {
  buildLabel, peelLabel, newLabelKey, HOP_RELAY, HOP_EXIT, HEADER_SIZE, MAX_HOPS, labelFromText,
} from "../src/index.mjs";
import { randomBytes } from "../src/bytes.mjs";

function route(n) {
  const keys = Array.from({ length: n }, () => newLabelKey());
  const hops = keys.map((k, i) => ({
    labelPub: k.pub,
    routing: {
      type: i === n - 1 ? HOP_EXIT : HOP_RELAY, holdMin: 1, holdMax: 3, boxClass: 1, weightClass: 2,
      category: 1, netGrams: 300 - i * 20, sleeveCode: randomBytes(8), custodyIn: randomBytes(16), custodyOut: randomBytes(16), ref: randomBytes(16),
    },
  }));
  return { keys, hops };
}

test("every route length peels hop by hop and each hop reads exactly its own slot", () => {
  for (let n = 1; n <= MAX_HOPS; n++) {
    const { keys, hops } = route(n);
    let label = buildLabel(hops).header;
    for (let i = 0; i < n; i++) {
      assert.equal(label.length, HEADER_SIZE, "constant size at every hop");
      const r = peelLabel(label, keys[i].priv);
      assert.deepEqual(r.routing.sleeveCode, hops[i].routing.sleeveCode);
      assert.deepEqual(r.routing.custodyOut, hops[i].routing.custodyOut);
      assert.equal(r.routing.netGrams, hops[i].routing.netGrams);
      if (i === n - 1) assert.equal(r.next, null);
      else label = r.next.header;
    }
  }
});

test("a hub can't peel a layer that isn't its own", () => {
  const { keys, hops } = route(3);
  const label = buildLabel(hops).header;
  assert.throws(() => peelLabel(label, keys[1].priv), /MAC mismatch/);
  assert.throws(() => peelLabel(label, newLabelKey().priv), /MAC mismatch/);
});

test("any flipped bit is detected", () => {
  const { keys, hops } = route(3);
  const label = buildLabel(hops).header;
  for (const pos of [3, 40, 60, 300, HEADER_SIZE - 1]) {
    const t = label.slice();
    t[pos] ^= 1;
    assert.throws(() => peelLabel(t, keys[0].priv));
  }
});

test("consecutive labels share no bytes in place (no physical/visual correlation)", () => {
  const { keys, hops } = route(4);
  let label = buildLabel(hops).header;
  for (let i = 0; i < 3; i++) {
    const next = peelLabel(label, keys[i].priv).next.header;
    let same = 0;
    for (let j = 3; j < HEADER_SIZE; j++) if (label[j] === next[j]) same++;
    // Random bytes collide ~1/256 of the time; anything far above that would be a leak.
    assert.ok(same < 20, `labels too similar: ${same} equal bytes`);
    label = next;
  }
});

test("label text round-trips and stays QR-sized", () => {
  const { hops } = route(5);
  const { header, text } = buildLabel(hops);
  assert.ok(text.startsWith("MN1."));
  assert.deepEqual(labelFromText(text), header);
  assert.ok(text.length < 1000, `label text ${text.length} chars`);
});

test("wrong hop types are rejected when building", () => {
  const { hops } = route(3);
  hops[2].routing.type = HOP_RELAY;
  assert.throws(() => buildLabel(hops), /exit/);
});
