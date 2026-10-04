// MuleNet simulator: one 3D-printed parcel (plus background traffic) travels through
// a network of friends' hubs, day by day. Everything except the carrier is the real
// @mulenet/core code: the labels are real, the address grants are really sealed,
// and each hub only processes what it scans.
//
//   node sim/run.mjs [--seed N] [--hops 3] [--parcels 10]
//
// Writes sim/out/: labels/*.svg (printable QR labels) and index.html (the visualization).
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import QRCode from "qrcode";
import { sha256 } from "@noble/hashes/sha2.js";
import {
  createHub, grantAddress, createRecipientMailbox, foldRegistry, newIdentity, vouch, merge,
  planRoute, processArrival, trackProgress, sleeveCodeText, categoryId, CATEGORIES, BOX_CLASSES,
} from "@mulenet/core";
import { renderHtml } from "./viz.mjs";

const args = Object.fromEntries(process.argv.slice(2).reduce((a, x, i, all) => (x.startsWith("--") ? [...a, [x.slice(2), all[i + 1]]] : a), []));
const SEED = Number(args.seed ?? 7);
const HOPS = Number(args.hops ?? 3);
const BACKGROUND = Number(args.parcels ?? 10);
const OUT = join(dirname(fileURLToPath(import.meta.url)), "out");
const DAY = 86400000;
const T0 = Date.UTC(2026, 9, 5, 9); // Monday 5 Oct 2026

// Deterministic randomness for reproducible runs (sha256 counter mode). Real use = CSPRNG.
let ctr = 0;
const rng = (n) => {
  const out = new Uint8Array(n);
  for (let o = 0; o < n; o += 32) {
    const block = sha256(new TextEncoder().encode(`mulenet-sim|${SEED}|${ctr++}`));
    out.set(block.slice(0, Math.min(32, n - o)), o);
  }
  return out;
};
const pick = (arr) => arr[rng(1)[0] % arr.length];
const intIn = (lo, hi) => lo + (rng(1)[0] % (hi - lo + 1));

// ── the network: friends in Logos Circle cities (logos.co/active-circles) ─────────
const ALL = ["*"];
const DECLARED = [1, 2, 3, 4];
const CITIES = [
  { name: "Prague mule", city: "Prague", country: "CZ", lat: 50.08, lon: 14.44, accepts: DECLARED, batchDays: [1, 4] },
  { name: "Berlin mule", city: "Berlin", country: "DE", lat: 52.52, lon: 13.4, accepts: ALL, batchDays: [2, 5] },
  { name: "Ruse mule", city: "Ruse", country: "BG", lat: 43.85, lon: 25.97, accepts: ALL, batchDays: [1, 3, 5] },
  { name: "Porto mule", city: "Porto", country: "PT", lat: 41.15, lon: -8.61, accepts: ALL, batchDays: [2, 4] },
  { name: "Lisboa mule", city: "Lisboa", country: "PT", lat: 38.72, lon: -9.14, accepts: DECLARED, batchDays: [1, 5] },
  { name: "Madrid mule", city: "Madrid", country: "ES", lat: 40.42, lon: -3.7, accepts: [1, 2, 6], batchDays: [3] },
];
// Pairs that did NOT grant each other an address (not every friend trusts every friend).
const NO_GRANT = new Set(["Ruse>Lisboa", "Lisboa>Ruse", "Prague>Porto"]);

const now = () => T0;
const steward = newIdentity(undefined, now);
const hubs = CITIES.map((c) => {
  const { hub, event } = createHub({
    name: c.name, city: c.city, country: c.country,
    policy: { accepts: c.accepts, boxClasses: [1, 2], maxWeightClass: 3, shipsTo: ["*"], batchDays: c.batchDays, holdMaxDays: 5 },
    intake: { kind: "meetup", text: `hand it over at the ${c.city} Logos Circle meetup` },
  }, { now });
  return { ...c, hub, event };
});
const byCity = Object.fromEntries(hubs.map((h) => [h.city, h]));
const cityOf = (addr) => hubs.find((h) => h.hub.address === addr)?.city;

let log = [...hubs.map((h) => h.event), ...hubs.map((h) => vouch(steward, h.hub.address))];
let state = foldRegistry(log, [steward.address]);
for (const to of hubs) for (const from of hubs) {
  if (to === from || NO_GRANT.has(`${from.city}>${to.city}`)) continue;
  log.push(grantAddress(to.hub, state.hubs.get(from.hub.address), {
    carrier: "parcel locker", locker: `${to.city.toUpperCase()}-LOCKER-${intIn(10, 99)}`, name: `${to.city} mule`, phone: "(private)",
  }));
}
state = foldRegistry(log, [steward.address]);

// Recipients create mailboxes at exit hubs; only the exit hub can read the pickup point.
const recipients = ["Madrid", "Berlin", "Porto", "Ruse"].map((city) => {
  const mb = createRecipientMailbox(state.hubs.get(byCity[city].hub.address), { carrier: "parcel locker", locker: `${city.toUpperCase()}-PICKUP-${intIn(100, 999)}`, name: "recipient" });
  log.push(mb.event);
  return { city, ...mb };
});
state = foldRegistry(merge(log), [steward.address]);

// ── parcels ──────────────────────────────────────────────────────────────────
const printed = categoryId("printed-object");
const parcels = [];
function send(id, fromCity, recipient, category, startDay, title) {
  const parcel = { category, boxClass: 1, weightClass: 1, coreGrams: 180 };
  const at = T0 + startDay * DAY;
  try {
    const route = planRoute(state, parcel, { entry: byCity[fromCity].hub.address, mailboxRef: recipient.card.mailboxRef, hops: HOPS, holdDays: [1, 4], atMs: at, rng });
    parcels.push({ id, title, from: fromCity, to: recipient.city, category, route, start: at, events: [], inbox: [], recipient });
  } catch (e) {
    parcels.push({ id, title, from: fromCity, to: recipient.city, category, route: null, error: e.message, start: at, events: [], inbox: [], recipient });
  }
}
send("hero", "Prague", recipients[0], printed, 0, "3D-printed mule figurine");
for (let i = 0; i < BACKGROUND; i++) {
  const from = pick(CITIES).city;
  const to = pick(recipients.filter((r) => r.city !== from));
  const cat = pick([printed, printed, 2, 3, 99]);
  send(`p${i + 1}`, from, to, cat, intIn(0, 6), `${CATEGORIES[cat]} parcel`);
}

// ── day-by-day event loop with a fake carrier (1-3 days transit) ──────────────
const queue = []; // { at, parcel, hop, label }
for (const p of parcels) if (p.route) queue.push({ at: p.start, parcel: p, hop: 0, label: p.route.label.text });
const observations = new Map(hubs.map((h) => [h.city, []])); // what each hub saw, by its own records
while (queue.length) {
  queue.sort((a, b) => a.at - b.at);
  const { at, parcel: p, hop, label } = queue.shift();
  const city = cityOf(p.route.path[hop]);
  const hub = byCity[city].hub;
  const d = processArrival(hub, state, {
    label, sleeveCode: sleeveCodeText(p.route.hops[hop].routing.sleeveCode), grossGrams: 450, boxGrams: 120, atMs: at,
  }, rng);
  p.inbox.push(...d.receipts);
  p.events.push({ hop, city, arrived: at, action: d.action, reason: d.reason, shipDay: d.ship?.day, padding: d.ship?.paddingGrams, sleeve: d.removeSleeve, nextLabel: d.ship?.label || null });
  observations.get(city).push({
    parcel: p.id, in: at, out: d.ship ? Date.parse(d.ship.day) : null,
    prev: hop === 0 ? "sender (meetup hand-over)" : cityOf(p.route.path[hop - 1]),
    next: d.action === "relay" ? cityOf(d.ship.nextHub) : d.action === "exit" ? "recipient's pickup locker" : "—",
    boxClass: d.routing.boxClass, weightClass: d.routing.weightClass, category: d.routing.category, action: d.action,
  });
  if (d.action === "relay") queue.push({ at: Date.parse(d.ship.day) + intIn(1, 3) * DAY + 10 * 3600000, parcel: p, hop: hop + 1, label: d.ship.label });
}

// ── what can an adversary link? ──────────────────────────────────────────────
// Model: every hub except one colludes and pools its records. The colluders see the
// hero parcel go INTO the honest hub, and every box that comes OUT of it. The hero is
// hidden among every outgoing box of the same class that left the honest hub after
// the hero went in, within the longest hold + batch window (5 + 7 days).
const hero = parcels[0];
function anonymitySet(p, honestCity) {
  const ev = p.events.find((e) => e.city === honestCity);
  if (!ev) return null;
  const seen = observations.get(honestCity).filter((o) => o.out && o.out >= ev.arrived && o.out <= ev.arrived + 12 * DAY && o.boxClass === 1 && o.weightClass === 1);
  return seen.length;
}
const adversary = hero.route ? hero.route.path.map((a) => ({ city: cityOf(a), set: anonymitySet(hero, cityOf(a)) })) : [];

// ── outputs ──────────────────────────────────────────────────────────────────
mkdirSync(join(OUT, "labels"), { recursive: true });
const day = (ms) => new Date(ms).toISOString().slice(0, 10);
// Every label the hero wore: the sender's, then each relay hub's reprint.
const heroLabels = [{ by: "sender", text: hero.route.label.text }, ...hero.events.filter((e) => e.nextLabel).map((e) => ({ by: e.city, text: e.nextLabel }))];
for (const [i, l] of heroLabels.entries()) {
  writeFileSync(join(OUT, "labels", `hero-${i}-${l.by.toLowerCase()}.svg`), await QRCode.toString(l.text, { type: "svg", errorCorrectionLevel: "M", margin: 2 }));
}
const heroTrack = trackProgress(hero.route.tracking, hero.inbox);

const html = renderHtml({
  seed: SEED,
  hops: HOPS,
  hubs: CITIES.map((c) => ({ city: c.city, country: c.country, lat: c.lat, lon: c.lon, accepts: c.accepts.includes("*") ? "anything" : c.accepts.map((x) => CATEGORIES[x]).join(", "), batchDays: c.batchDays })),
  grants: [...state.grants.values()].map((g) => ({ from: cityOf(g.from), to: cityOf(g.to) })),
  hero: {
    title: hero.title, from: hero.from, to: hero.to,
    path: hero.route.path.map(cityOf),
    events: hero.events.map(({ nextLabel, ...e }) => ({ ...e, arrived: day(e.arrived) })),
    labels: heroLabels.map((l) => ({ by: l.by, head: l.text.slice(0, 28) })),
    track: heroTrack.map((s) => ({ index: s.index, receipts: s.receipts.map((r) => ({ kind: r.kind, day: r.day, shipDay: r.shipDay, reason: r.reason })) })),
    packing: hero.route.packing,
    label: hero.route.label.text,
    labelQr: await QRCode.toString(hero.route.label.text, { type: "svg", errorCorrectionLevel: "M", margin: 1 }),
  },
  parcels: parcels.map((p) => ({ id: p.id, title: p.title, from: p.from, to: p.to, error: p.error || null, delivered: p.events.some((e) => e.action === "exit"), refused: p.events.find((e) => e.action === "refuse")?.reason || null, hopsDone: p.events.length })),
  observations: Object.fromEntries([...observations].map(([c, os]) => [c, os.map((o) => ({ ...o, in: day(o.in), out: o.out ? day(o.out) : null, category: CATEGORIES[o.category] }))])),
  adversary,
  boxClass: BOX_CLASSES[1],
});
writeFileSync(join(OUT, "index.html"), html);

// ── console narrative ────────────────────────────────────────────────────────
console.log(`MuleNet simulation (seed ${SEED}, ${HOPS} hops, ${parcels.length} parcels)\n`);
console.log(`Hero: "${hero.title}"  ${hero.from} → ${hero.to}`);
console.log(`Route (known only to the sender): ${hero.route.path.map(cityOf).join(" → ")}`);
console.log(`Label: ${hero.route.label.text.length} chars, same size at every hop\n`);
console.log("Packing list (outermost sleeve first):");
for (const s of hero.route.packing) console.log(`  sleeve ${s.sleeve}: seal ${s.code}  (removed by ${s.removedBy})`);
console.log("\nJourney:");
for (const e of hero.events) console.log(`  ${day(e.arrived)}  ${e.city.padEnd(7)} ${e.action.padEnd(6)} removes sleeve ${e.sleeve ?? "-"}  ships ${e.shipDay ?? "-"}  pad ${e.padding ?? "-"} g${e.reason ? "  (" + e.reason + ")" : ""}`);
console.log("\nSender's tracker (decrypted receipts):");
for (const s of heroTrack) console.log(`  custody step ${s.index}: ${s.receipts.map((r) => `${r.kind}@${r.day}`).join(", ") || "(nothing yet)"}`);
console.log("\nIf every other hub colludes, the hero hides among:");
for (const a of adversary) console.log(`  honest ${a.city.padEnd(7)}: ${a.set} indistinguishable box(es)${a.set <= 1 ? "  ← linkable: not enough traffic" : ""}`);
const delivered = parcels.filter((p) => p.events.some((e) => e.action === "exit")).length;
console.log(`\nBackground: ${delivered}/${parcels.length} delivered, ${parcels.filter((p) => p.error).length} unroutable, ${parcels.filter((p) => p.events.some((e) => e.action === "refuse")).length} refused`);
for (const p of parcels.filter((p) => p.error || p.events.some((e) => e.action === "refuse"))) console.log(`  ${p.id} ${p.title} ${p.from}→${p.to}: ${p.error || p.events.find((e) => e.action === "refuse").reason}`);
console.log(`\nWrote ${join(OUT, "index.html")}`);
