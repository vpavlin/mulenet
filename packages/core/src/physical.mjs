// physical.mjs — the physical half of the onion (docs/adr/0002, docs/SPEC.md §5-6).
//
// A parcel is a "core" (the item in its own plain inner packaging) wrapped in one
// tamper-sealed SLEEVE per hop, outermost first. Hop i removes sleeve i, checks its
// seal code against its label slot, and ships the rest in a FRESH outer box of the
// standard class, padded to the standard weight band. So:
//   - no outer box or label survives a hop (the re-box breaks visual correlation);
//   - sleeve i is seen only by hop i-1 (which exposed it) and hop i (which removes it):
//     two colluding hubs either side of an honest hub never saw the same object;
//   - nobody opens the item. Only envelopes come off (docs/adr/0008).

/** Outer box classes, sized to fit common parcel-locker compartments. */
export const BOX_CLASSES = {
  1: { name: "S", dims: "25×18×8 cm" },
  2: { name: "M", dims: "35×25×15 cm" },
  3: { name: "L", dims: "50×35×20 cm" },
};

/** Gross weight bands. A hub pads the box to `target` grams (±tolerance). */
export const WEIGHT_CLASSES = {
  1: { max: 500, target: 450 },
  2: { max: 1000, target: 900 },
  3: { max: 2000, target: 1800 },
  4: { max: 5000, target: 4500 },
};
export const WEIGHT_TOLERANCE_G = 25;

/** Declared content categories (u16 on the label). 0 is invalid. */
export const CATEGORIES = {
  1: "printed-object",       // 3D prints, plastic/resin objects
  2: "paper",                // books, documents, art prints
  3: "textiles",
  4: "electronics-no-battery",
  5: "electronics-with-battery",
  6: "hardware-tools",
  7: "sealed-food",
  99: "undeclared",          // "sealed-any": only hubs that relay anything accept it
};

export function categoryId(name) {
  const hit = Object.entries(CATEGORIES).find(([, v]) => v === name);
  if (!hit) throw new Error("unknown category " + name);
  return Number(hit[0]);
}

/** Smallest weight class whose target can hold `grams` of contents plus packaging. */
export function weightClassFor(grams, packagingG = 150) {
  for (const [k, w] of Object.entries(WEIGHT_CLASSES)) if (grams + packagingG <= w.target) return Number(k);
  throw new Error(`parcel too heavy (${grams} g)`);
}

/** Padding a hub adds so the box it ships hits the band's target weight. */
export function paddingFor(weightClass, contentsG, boxG) {
  const w = WEIGHT_CLASSES[weightClass];
  if (!w) throw new Error("unknown weight class " + weightClass);
  const pad = w.target - contentsG - boxG;
  if (pad < -WEIGHT_TOLERANCE_G) throw new Error(`contents exceed weight class ${weightClass}`);
  return Math.max(0, pad);
}

// Sleeve seal codes: 8 random bytes printed as Crockford base32 (13 chars, 4-4-5).
const CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
export function sleeveCodeText(bytes) {
  let bits = 0n;
  for (const b of bytes) bits = (bits << 8n) | BigInt(b);
  let s = "";
  for (let i = 0; i < 13; i++) { s = CROCKFORD[Number(bits & 31n)] + s; bits >>= 5n; }
  return `${s.slice(0, 4)}-${s.slice(4, 8)}-${s.slice(8)}`;
}
export function sleeveCodeFromText(text) {
  const t = text.toUpperCase().replace(/[^0-9A-Z]/g, "").replace(/O/g, "0").replace(/[IL]/g, "1");
  if (t.length !== 13) throw new Error("sleeve code must be 13 characters");
  let bits = 0n;
  for (const c of t) {
    const v = CROCKFORD.indexOf(c);
    if (v < 0) throw new Error("bad sleeve code character " + c);
    bits = (bits << 5n) | BigInt(v);
  }
  const out = new Uint8Array(8);
  for (let i = 7; i >= 0; i--) { out[i] = Number(bits & 255n); bits >>= 8n; }
  return out;
}

/** The sender's packing list for a route: sleeves outermost-first. */
export function packingPlan(route) {
  return route.hops.map((h, i) => ({
    sleeve: i + 1,
    removedBy: `hop ${i + 1}`,
    code: sleeveCodeText(h.routing.sleeveCode),
  }));
}
