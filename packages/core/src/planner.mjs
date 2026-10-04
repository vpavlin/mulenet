// planner.mjs — pick a route and build everything the sender needs (docs/SPEC.md §8).
//
// Runs entirely on the sender's device, from its local copy of the registry: no
// server ever learns which route was chosen. Hubs are chosen uniformly at random
// among those that satisfy every constraint, so a route is not predictable from
// the parcel's properties.
import { buildLabel, HOP_RELAY, HOP_EXIT, MAX_HOPS } from "./label.mjs";
import { grantFor, liveLabelKey } from "./registry.mjs";
import { randomBytes, fromHex } from "./bytes.mjs";
import { packingPlan } from "./physical.mjs";

export const DEFAULT_SLEEVE_G = 20;

/** Does a hub's published policy accept this parcel? */
export function policyAccepts(hub, parcel) {
  const p = hub.policy || {};
  const acc = p.accepts || [];
  if (!(acc.includes("*") || acc.includes(parcel.category))) return false;
  if (Array.isArray(p.boxClasses) && !p.boxClasses.includes(parcel.boxClass)) return false;
  if (p.maxWeightClass && parcel.weightClass > p.maxWeightClass) return false;
  return true;
}

function shipsTo(hub, country) {
  const s = hub.policy?.shipsTo || [];
  return s.includes("*") || s.includes(country);
}

function eligible(hub, parcel, minVouches) {
  return hub.vouchedBy.length >= minVouches && policyAccepts(hub, parcel) && hub.labelKeys.length > 0;
}

function shuffle(a, rng) {
  const out = a.slice();
  for (let i = out.length - 1; i > 0; i--) {
    const j = rng(4).reduce((x, b) => (x * 256 + b) >>> 0, 0) % (i + 1);
    [out[i], out[j]] = [out[j], out[i]];
  }
  return out;
}

/**
 * Find a hub path entry → … → exit with exactly `hops` hubs, every consecutive pair
 * linked by an address grant, every hub accepting the parcel.
 * @returns [hubAddress] or null
 */
export function findPath(state, { entry, exit, hops, parcel, minVouches = 1, rng = randomBytes, distinctCities = true }) {
  const hub = (a) => state.hubs.get(a);
  if (!hub(entry) || !hub(exit)) return null;
  if (!eligible(hub(entry), parcel, minVouches) || !eligible(hub(exit), parcel, minVouches)) return null;
  if (hops === 1) return entry === exit ? [entry] : null;

  const path = [entry];
  const dfs = () => {
    const cur = path[path.length - 1];
    if (path.length === hops - 1) {
      return grantFor(state, cur, exit) && shipsTo(hub(cur), hub(exit).country) && !path.includes(exit) ? (path.push(exit), true) : false;
    }
    const next = shuffle([...state.hubs.keys()], rng).filter((a) =>
      a !== exit && !path.includes(a) && eligible(hub(a), parcel, minVouches) &&
      grantFor(state, cur, a) && shipsTo(hub(cur), hub(a).country) &&
      (!distinctCities || !path.some((p) => hub(p).city === hub(a).city)));
    for (const a of next) {
      path.push(a);
      if (dfs()) return true;
      path.pop();
    }
    return false;
  };
  return dfs() ? path : null;
}

/**
 * Plan a full route and build the label.
 * @param parcel { category, boxClass, weightClass, coreGrams, sleeveGrams? }
 * @param opts   { entry, mailboxRef, hops (default 3), holdDays: [min,max], atMs, minVouches, rng }
 * @returns { path, label, hops: [{hub, routing}], tracking, packing } — `tracking` is the
 *          sender's private record (custody tokens), kept on-device only.
 */
export function planRoute(state, parcel, opts) {
  const rng = opts.rng || randomBytes;
  const n = opts.hops ?? 3;
  if (n < 1 || n > MAX_HOPS) throw new Error(`route length must be 1..${MAX_HOPS}`);
  const mailbox = state.mailboxes.get(opts.mailboxRef);
  if (!mailbox) throw new Error("unknown mailbox");
  const minVouches = opts.minVouches ?? 1;
  // Name the specific reason when an endpoint can't take the parcel; it's the common case.
  for (const [role, addr] of [["entry", opts.entry], ["exit", mailbox.exit]]) {
    const h = state.hubs.get(addr);
    if (!h) throw new Error(`${role} hub is not in the directory`);
    if (h.vouchedBy.length < minVouches) throw new Error(`${role} hub ${h.name} has no steward vouch`);
    if (!policyAccepts(h, parcel)) throw new Error(`${role} hub ${h.name} does not carry this parcel (category/size policy)`);
  }
  const path = findPath(state, { entry: opts.entry, exit: mailbox.exit, hops: n, parcel, minVouches, rng });
  if (!path) throw new Error("no route satisfies the constraints (try fewer hops, another entry hub, or a broader category)");

  const at = opts.atMs ?? Date.now();
  const [holdMin, holdMax] = opts.holdDays || [1, 4];
  const sleeveG = parcel.sleeveGrams ?? DEFAULT_SLEEVE_G;
  // custody[i] is shared by hop i-1 (out) and hop i (in); custody[0] = sender → entry,
  // custody[n] = exit → sender ("delivered to the locker").
  const custody = Array.from({ length: n + 1 }, () => rng(16));

  const hops = path.map((addr, i) => {
    const hub = state.hubs.get(addr);
    const labelPub = liveLabelKey(hub, at);
    if (!labelPub) throw new Error(`hub ${hub.name} has no live label key`);
    const cap = hub.policy?.holdMaxDays ?? 14;
    const lo = Math.min(holdMin, cap);
    const hi = Math.min(Math.max(holdMax, lo), cap);
    const isExit = i === n - 1;
    const ref = isExit ? opts.mailboxRef : grantFor(state, addr, path[i + 1]).ref;
    return {
      hub: addr,
      labelPub,
      routing: {
        type: isExit ? HOP_EXIT : HOP_RELAY,
        holdMin: lo,
        holdMax: hi,
        boxClass: parcel.boxClass,
        weightClass: parcel.weightClass,
        category: parcel.category,
        netGrams: parcel.coreGrams + (n - i) * sleeveG,
        sleeveCode: rng(8),
        custodyIn: custody[i],
        custodyOut: custody[i + 1],
        ref: fromHex(ref),
      },
    };
  });

  const label = buildLabel(hops.map((h) => ({ labelPub: h.labelPub, routing: h.routing })), rng);
  return {
    path,
    label,
    hops: hops.map(({ hub, routing }) => ({ hub, routing })),
    packing: packingPlan({ hops }),
    tracking: { custody, createdAt: at, path },
  };
}
