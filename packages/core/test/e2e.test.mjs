import test from "node:test";
import assert from "node:assert/strict";
import {
  planRoute, processArrival, trackProgress, foldRegistry, merge, openReceipt, custodyTopic,
  sleeveCodeText, revokeGrant, retire, categoryId, grantFor,
} from "../src/index.mjs";
import { network, PARCEL, T0 } from "./fixtures.mjs";

const DAY = 86400000;

function walk(net, route, { tamperAt = -1 } = {}) {
  let label = route.label.text;
  let at = T0;
  const inbox = [];
  const log = [];
  for (let i = 0; i < route.path.length; i++) {
    const hub = net.hubs.find((h) => h.address === route.path[i]);
    const code = sleeveCodeText(route.hops[i].routing.sleeveCode);
    const d = processArrival(hub, net.state, { label, sleeveCode: i === tamperAt ? "0000-0000-00000" : code, grossGrams: 430, boxGrams: 120, atMs: at });
    inbox.push(...d.receipts);
    log.push(d);
    if (d.action === "refuse") break;
    label = d.ship.label;
    at = Date.parse(d.ship.day) + 2 * DAY; // carrier transit
  }
  return { log, inbox };
}

test("a parcel travels entry → middle → exit and only the right people learn each fact", () => {
  const net = network();
  const [prague, , , , madrid] = net.hubs;
  const route = planRoute(net.state, PARCEL, { entry: prague.address, mailboxRef: net.mailbox.card.mailboxRef, hops: 3, atMs: T0 });
  assert.equal(route.path[0], prague.address);
  assert.equal(route.path[2], madrid.address);

  const { log, inbox } = walk(net, route);
  assert.deepEqual(log.map((d) => d.action), ["relay", "relay", "exit"]);

  // Each relay hub learned exactly its next hop's private address and nothing further.
  assert.equal(log[0].ship.nextHub, route.path[1]);
  assert.equal(log[1].ship.nextHub, route.path[2]);
  assert.match(log[1].ship.address.locker, /^MAD-/);
  // Only the exit hub learned the recipient's pickup point.
  assert.equal(log[2].ship.address.locker, "MAD-CITYLOCKER-7");
  assert.ok(!JSON.stringify(log.slice(0, 2)).includes("CITYLOCKER"));

  // The sender's tracker sees every step; the recipient gets a "ready" notice.
  const steps = trackProgress(route.tracking, inbox);
  assert.deepEqual(steps.map((s) => s.receipts.map((r) => r.kind).sort()), [["received"], ["received", "shipped"], ["received", "shipped"], ["delivered"]]);
  const notify = net.mailbox.notifyToken;
  const ready = inbox.filter((m) => m.topic === custodyTopic(notify)).map((m) => openReceipt(notify, m.topic, m.bytes));
  assert.equal(ready[0].kind, "ready");
});

test("the sender's plan never contains a hub address or the recipient's pickup point", () => {
  const net = network();
  const route = planRoute(net.state, PARCEL, { entry: net.hubs[0].address, mailboxRef: net.mailbox.card.mailboxRef, hops: 3, atMs: T0 });
  const everything = JSON.stringify(route, (k, v) => (v instanceof Uint8Array ? Array.from(v) : v));
  assert.ok(!everything.includes("InPost") && !everything.includes("CITYLOCKER") && !everything.includes("locker"));
});

test("receipts are unreadable without the custody token", () => {
  const net = network();
  const route = planRoute(net.state, PARCEL, { entry: net.hubs[0].address, mailboxRef: net.mailbox.card.mailboxRef, hops: 3, atMs: T0 });
  const { inbox } = walk(net, route);
  const wrong = new Uint8Array(16);
  for (const m of inbox) assert.equal(openReceipt(wrong, m.topic, m.bytes), null);
});

test("a swapped sleeve is refused and reported back, the parcel goes no further", () => {
  const net = network();
  const route = planRoute(net.state, PARCEL, { entry: net.hubs[0].address, mailboxRef: net.mailbox.card.mailboxRef, hops: 3, atMs: T0 });
  const { log, inbox } = walk(net, route, { tamperAt: 1 });
  assert.deepEqual(log.map((d) => d.action), ["relay", "refuse"]);
  assert.match(log[1].reason, /tampering/);
  const steps = trackProgress(route.tracking, inbox);
  assert.equal(steps[1].receipts.find((r) => r.kind === "refused").reason, log[1].reason);
});

test("a re-sent copy of the same label is refused (replay)", () => {
  const net = network();
  const route = planRoute(net.state, PARCEL, { entry: net.hubs[0].address, mailboxRef: net.mailbox.card.mailboxRef, hops: 3, atMs: T0 });
  const scan = { label: route.label.text, sleeveCode: sleeveCodeText(route.hops[0].routing.sleeveCode), atMs: T0 };
  assert.equal(processArrival(net.hubs[0], net.state, scan).action, "relay");
  assert.match(processArrival(net.hubs[0], net.state, scan).reason, /replay/);
});

test("hubs that only carry declared goods are routed around for 'undeclared' parcels", () => {
  const net = network({ accepts: [1, 2, 3] });
  const undeclared = { ...PARCEL, category: categoryId("undeclared") };
  assert.throws(() => planRoute(net.state, undeclared, { entry: net.hubs[0].address, mailboxRef: net.mailbox.card.mailboxRef, hops: 3 }), /no route/);
  // and a hub refuses one that reaches it anyway
  const ok = planRoute(net.state, PARCEL, { entry: net.hubs[0].address, mailboxRef: net.mailbox.card.mailboxRef, hops: 3, atMs: T0 });
  net.hubs[0].policy = { ...net.hubs[0].policy, accepts: [2] };
  const d = processArrival(net.hubs[0], net.state, { label: ok.label.text, sleeveCode: sleeveCodeText(ok.hops[0].routing.sleeveCode), atMs: T0 });
  assert.match(d.reason, /category not accepted/);
});

test("revoked grants and retired hubs drop out of the directory whatever the arrival order", () => {
  const net = network();
  const [a, b] = net.hubs;
  const g = grantFor(net.state, a.address, b.address);
  const extra = [revokeGrant(b.identity, g.ref), retire(net.hubs[3].identity)];
  const forward = foldRegistry(merge(net.log, extra), [net.steward.address]);
  const backward = foldRegistry([...extra, ...net.log.slice().reverse()], [net.steward.address]);
  for (const s of [forward, backward]) {
    assert.equal(grantFor(s, a.address, b.address), null);
    assert.ok(!s.hubs.has(net.hubs[3].address));
  }
  assert.deepEqual([...forward.grants.keys()].sort(), [...backward.grants.keys()].sort());
});

test("unsigned or forged registry events are ignored", () => {
  const net = network();
  const forged = structuredClone(net.log[0]);
  forged.payload.city = "Nowhere";
  const s = foldRegistry([...net.log, forged], [net.steward.address]);
  assert.equal(s.hubs.get(net.hubs[0].address).city, "Prague");
  const unvouched = foldRegistry(net.log.filter((e) => e.type !== "vouch"), [net.steward.address]);
  assert.throws(() => planRoute(unvouched, PARCEL, { entry: net.hubs[0].address, mailboxRef: net.mailbox.card.mailboxRef, hops: 3 }), /no route/);
});

test("hold days and batch days push the ship date forward", () => {
  const net = network();
  const route = planRoute(net.state, PARCEL, { entry: net.hubs[0].address, mailboxRef: net.mailbox.card.mailboxRef, hops: 3, holdDays: [3, 3], atMs: T0 });
  const d = processArrival(net.hubs[0], net.state, { label: route.label.text, sleeveCode: sleeveCodeText(route.hops[0].routing.sleeveCode), atMs: T0 });
  // Monday + 3 days = Thursday, which is a batch day (1 = Mon, 4 = Thu).
  assert.equal(d.ship.day, "2026-10-08");
});
