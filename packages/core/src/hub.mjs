// hub.mjs — what a hub's app does when a parcel arrives (docs/SPEC.md §9).
//
// Pure logic, fully offline: scan the label, check the sleeve and the weight, peel
// the layer, open the next address, schedule the shipment, queue receipts. Nothing
// here needs the network; receipts are posted whenever the hub is next online.
import { peelWithAnyKey } from "./label.mjs";
import { openSealed } from "./seal.mjs";
import { makeReceipt, dayOf } from "./receipts.mjs";
import { CATEGORIES, WEIGHT_CLASSES, WEIGHT_TOLERANCE_G, paddingFor, sleeveCodeFromText, sleeveCodeText } from "./physical.mjs";
import { hex, equalBytes, fromB64url, randomBytes } from "./bytes.mjs";

const DAY = 86400000;

export const grantContext = (from, to) => `mulenet-grant-v1|${from}|${to}`;
export const mailboxContext = (exit, ref) => `mulenet-mailbox-v1|${exit}|${ref}`;

function uniformInt(lo, hi, rng) {
  if (hi <= lo) return lo;
  const x = rng(4).reduce((a, b) => (a * 256 + b) >>> 0, 0);
  return lo + (x % (hi - lo + 1));
}

/** First batch day on/after `ms` (policy.batchDays = UTC weekdays, 0 = Sunday). */
export function nextBatchDay(ms, batchDays) {
  if (!batchDays || batchDays.length === 0) return ms;
  for (let d = 0; d < 7; d++) {
    const t = ms + d * DAY;
    if (batchDays.includes(new Date(t).getUTCDay())) return t;
  }
  return ms;
}

/**
 * Process an arriving parcel.
 * @param hub    { address, labelPrivs:[Uint8Array], boxPriv, policy, seen:Set<hex> }
 * @param state  folded registry (for grants + mailboxes)
 * @param scan   { label (text), sleeveCode (text the hub typed/scanned), grossGrams, boxGrams, atMs }
 * @returns a decision:
 *   { action: "relay"|"exit", ship: { day, address, label?, boxClass, weightClass, paddingGrams },
 *     receipts: [{topic, bytes, postAfterMs}], removeSleeve: code }
 *   { action: "refuse", reason, receipts }
 */
export function processArrival(hub, state, scan, rng = randomBytes) {
  const at = scan.atMs ?? Date.now();
  const peeled = peelWithAnyKey(scan.label, hub.labelPrivs); // throws: not ours / tampered label
  const r = peeled.routing;
  const tag = hex(peeled.replayTag);
  const receipts = [];
  const post = (token, body, whenMs = at) => {
    // Post receipts after a random 2-30 h delay so they don't line up with carrier scans.
    const delay = uniformInt(2, 30, rng) * 3600000;
    receipts.push({ ...makeReceipt(token, { ...body, day: dayOf(whenMs) }), postAfterMs: whenMs + delay });
  };
  const refuse = (reason) => {
    post(r.custodyIn, { kind: "refused", reason });
    return { action: "refuse", reason, routing: r, receipts };
  };

  if (hub.seen.has(tag)) return { action: "refuse", reason: "replay: this label was already processed", routing: r, receipts: [] };
  hub.seen.add(tag);

  // Policy: the hub decides what it carries (docs/adr/0008). It never opens the item.
  const acc = hub.policy.accepts || [];
  if (!(acc.includes("*") || acc.includes(r.category))) return refuse(`category not accepted: ${CATEGORIES[r.category] || r.category}`);

  // Tamper check: the sleeve this hub removes must carry the code the sender put in its slot.
  let typed;
  try { typed = sleeveCodeFromText(scan.sleeveCode); } catch { return refuse("sleeve seal code unreadable"); }
  if (!equalBytes(typed, r.sleeveCode)) return refuse("sleeve seal code does not match (possible tampering)");

  // Weight check: weighing is allowed, opening is not.
  if (typeof scan.grossGrams === "number" && typeof scan.boxGrams === "number") {
    const net = scan.grossGrams - scan.boxGrams;
    // The arriving box was padded to its band, so allow the band's padding on top of the declaration.
    const band = WEIGHT_CLASSES[r.weightClass];
    if (!band || net > band.target + WEIGHT_TOLERANCE_G) return refuse(`overweight for its class (${net} g)`);
  }

  post(r.custodyIn, { kind: "received", sleeve: "intact" });

  const holdDays = uniformInt(r.holdMin, r.holdMax, rng);
  const shipMs = nextBatchDay(at + holdDays * DAY, hub.policy.batchDays);
  const outBoxGrams = hub.boxGrams?.[r.boxClass] ?? 120;
  const paddingGrams = paddingFor(r.weightClass, r.netGrams - (hub.sleeveGrams ?? 20), outBoxGrams);

  if (peeled.next) {
    const g = state.grants.get(hex(r.ref));
    if (!g || g.from !== hub.address) return refuse("no address grant for the next hop (revoked?)");
    let address;
    try { address = openSealed(hub.boxPriv, fromB64url(g.sealed), grantContext(g.from, g.to)); } catch { return refuse("next hop's address grant can't be opened"); }
    post(r.custodyOut, { kind: "shipped" }, shipMs);
    return {
      action: "relay",
      routing: r,
      removeSleeve: sleeveCodeText(r.sleeveCode),
      ship: { day: dayOf(shipMs), address, nextHub: g.to, label: peeled.next.text, boxClass: r.boxClass, weightClass: r.weightClass, paddingGrams },
      receipts,
    };
  }

  // Exit: deliver to the recipient's pickup point.
  const ref = hex(r.ref);
  const m = state.mailboxes.get(ref);
  if (!m || m.exit !== hub.address) return refuse("unknown mailbox");
  let pickup;
  try { pickup = openSealed(hub.boxPriv, fromB64url(m.sealed), mailboxContext(m.exit, ref)); } catch { return refuse("mailbox can't be opened"); }
  post(r.custodyOut, { kind: "delivered" }, shipMs);
  if (pickup.notify) {
    const t = Uint8Array.from(pickup.notify.match(/../g).map((h) => parseInt(h, 16)));
    receipts.push({ ...makeReceipt(t, { kind: "ready", day: dayOf(shipMs) }), postAfterMs: shipMs });
  }
  return {
    action: "exit",
    routing: r,
    removeSleeve: sleeveCodeText(r.sleeveCode),
    ship: { day: dayOf(shipMs), address: pickup, label: null, boxClass: r.boxClass, weightClass: r.weightClass, paddingGrams },
    receipts,
  };
}
