// viz.mjs — renders the simulation as one self-contained HTML page with four views:
//   Sender    — the route and the decrypted custody receipts (only the sender has these)
//   Hub       — exactly what one hub knows from its own records
//   Public    — what anyone can see: the directory, no routes
//   Adversary — how well the hero parcel hides if every other hub colludes
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);

export function renderHtml(data) {
  const json = JSON.stringify(data).replace(/</g, "\\u003c");
  return `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MuleNet simulation</title>
<style>
.viz-root{color-scheme:light;--surface-0:#f4f3ef;--surface-1:#fcfcfb;--border:#dcdad3;--grid:#e8e6e0;
  --text-primary:#0b0b0b;--text-secondary:#52514e;--text-muted:#7a7972;--series-1:#2a78d6;--node:#8a8983;--edge:#c9c7bf;
  --good:#008300;--critical:#e34948}
@media (prefers-color-scheme:dark){:root:where(:not([data-theme="light"])) .viz-root{color-scheme:dark;--surface-0:#121211;--surface-1:#1a1a19;
  --border:#383835;--grid:#2a2a28;--text-primary:#fff;--text-secondary:#c3c2b7;--text-muted:#8f8e86;--series-1:#3987e5;--node:#8f8e86;--edge:#46463f;--good:#2fa52f;--critical:#e66767}}
:root[data-theme="dark"] .viz-root{color-scheme:dark;--surface-0:#121211;--surface-1:#1a1a19;--border:#383835;--grid:#2a2a28;
  --text-primary:#fff;--text-secondary:#c3c2b7;--text-muted:#8f8e86;--series-1:#3987e5;--node:#8f8e86;--edge:#46463f;--good:#2fa52f;--critical:#e66767}
*{box-sizing:border-box}
body{margin:0;font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
.viz-root{background:var(--surface-0);color:var(--text-primary);min-height:100vh;padding:24px}
h1{font-size:22px;margin:0 0 4px}h2{font-size:16px;margin:0 0 12px}h3{font-size:13px;margin:16px 0 6px;color:var(--text-secondary);text-transform:uppercase;letter-spacing:.04em}
p.lead{color:var(--text-secondary);margin:0 0 16px;max-width:900px}
.tabs{display:flex;gap:6px;margin-bottom:16px;flex-wrap:wrap}
.tabs button{background:var(--surface-1);color:var(--text-primary);border:1px solid var(--border);border-radius:8px;padding:6px 14px;cursor:pointer;font:inherit}
.tabs button[aria-selected="true"]{border-color:var(--series-1);box-shadow:inset 0 -2px 0 var(--series-1)}
.grid{display:grid;grid-template-columns:minmax(0,1.3fr) minmax(0,1fr);gap:16px}
@media (max-width:900px){.grid{grid-template-columns:1fr}}
.card{background:var(--surface-1);border:1px solid var(--border);border-radius:12px;padding:16px}
svg text{fill:var(--text-secondary);font-size:11px}
table{border-collapse:collapse;width:100%;font-size:13px}
th,td{text-align:left;padding:5px 8px;border-bottom:1px solid var(--grid);vertical-align:top}
th{color:var(--text-secondary);font-weight:600}
.muted{color:var(--text-muted)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;word-break:break-all}
.tl{list-style:none;margin:0;padding:0}
.tl li{padding:6px 0 6px 18px;border-left:2px solid var(--grid);position:relative}
.tl li::before{content:"";position:absolute;left:-6px;top:11px;width:10px;height:10px;border-radius:50%;background:var(--series-1);box-shadow:0 0 0 2px var(--surface-1)}
.tl li.pending::before{background:var(--node)}
.status{display:inline-flex;gap:4px;align-items:center;font-weight:600}
.status.bad{color:var(--critical)}.status.ok{color:var(--good)}
.qr{max-width:240px;background:#fff;padding:6px;border-radius:8px}
#tip{position:fixed;pointer-events:none;background:var(--surface-1);color:var(--text-primary);border:1px solid var(--border);border-radius:8px;padding:6px 10px;font-size:12px;box-shadow:0 4px 14px rgba(0,0,0,.15);display:none;max-width:280px;z-index:9}
select{font:inherit;padding:4px 8px;border-radius:6px;border:1px solid var(--border);background:var(--surface-1);color:var(--text-primary)}
.view{display:none}.view.on{display:block}
.note{border-left:3px solid var(--border);padding:4px 10px;color:var(--text-secondary);margin:10px 0}
</style></head>
<body><div class="viz-root">
<h1>MuleNet — physical mixnet simulation</h1>
<p class="lead">One <b id="heroTitle"></b> travels from <b id="heroFrom"></b> to a pickup locker in <b id="heroTo"></b> through friends' hubs in Logos Circle cities.
Each hub peels one layer of the label, removes one sealed sleeve, re-boxes the parcel and ships it on. Seed <span id="seed"></span>, <span id="hops"></span> hops.</p>
<div class="tabs" role="tablist">
  <button role="tab" data-v="sender" aria-selected="true">Sender view</button>
  <button role="tab" data-v="hub" aria-selected="false">Hub view</button>
  <button role="tab" data-v="public" aria-selected="false">Public view</button>
  <button role="tab" data-v="adversary" aria-selected="false">Adversary</button>
</div>

<section class="view on" id="v-sender"><div class="grid">
  <div class="card"><h2>The route (only the sender knows it)</h2><svg id="mapSender" viewBox="0 0 640 460" role="img" aria-label="Map of the hero parcel's route"></svg></div>
  <div class="card"><h2>Custody receipts</h2><p class="muted">Decrypted on the sender's device from Logos Messaging. Day resolution only.</p><ul class="tl" id="timeline"></ul>
    <h3>Packing list</h3><table id="packing"><thead><tr><th>Sleeve</th><th>Seal code</th><th>Removed by</th></tr></thead><tbody></tbody></table>
    <h3>Label printed on the first box</h3><div id="qr"></div><p class="mono muted" id="labelText"></p>
    <h3>Every label the parcel wore</h3><table id="labels"><thead><tr><th>Printed by</th><th>Starts with</th></tr></thead><tbody></tbody></table>
    <p class="muted">Same size every time, no shared bytes: a hub can't tell how far the parcel has come or still has to go.</p>
  </div>
</div></section>

<section class="view" id="v-hub"><div class="grid">
  <div class="card"><h2>What <select id="hubSel"></select> knows</h2><svg id="mapHub" viewBox="0 0 640 460" role="img" aria-label="Map of what one hub knows"></svg></div>
  <div class="card"><h2>Its own records</h2><table id="hubTable"><thead><tr><th>In</th><th>From</th><th>Out</th><th>To</th><th>Declared</th></tr></thead><tbody></tbody></table>
    <div class="note">It knows its neighbours (who shipped to it, where it ships next) and nothing else: not the sender, not the recipient, not the rest of the route, not what's inside.</div></div>
</div></section>

<section class="view" id="v-public"><div class="grid">
  <div class="card"><h2>The directory: anyone can see this</h2><svg id="mapPublic" viewBox="0 0 640 460" role="img" aria-label="Map of public hub directory"></svg>
    <p class="muted">Lines are address grants ("this hub may ship to that one"). The trust graph is public; the addresses aren't.</p></div>
  <div class="card"><h2>Hubs and policies</h2><table id="pubTable"><thead><tr><th>City</th><th>Carries</th><th>Ships on</th></tr></thead><tbody></tbody></table>
    <h3>All parcels in this run</h3><table id="parcelTable"><thead><tr><th>Parcel</th><th>Route endpoints</th><th>Outcome</th></tr></thead><tbody></tbody></table>
    <p class="muted">Endpoints are shown here because this is a simulation. In the real network nobody sees this table.</p></div>
</div></section>

<section class="view" id="v-adversary"><div class="grid">
  <div class="card"><h2>If every other hub colludes, how many boxes does the hero hide among?</h2>
    <svg id="anon" viewBox="0 0 600 300" role="img" aria-label="Anonymity set per honest hub"></svg>
    <table id="anonTable"><thead><tr><th>Honest hub</th><th>Indistinguishable boxes</th></tr></thead><tbody></tbody></table></div>
  <div class="card"><h2>How to read this</h2>
    <p>A mixnet only needs <b>one honest hop</b>. Suppose every hub except one pools its records. The colluders saw the hero go <i>into</i> the honest hub. Every same-class box that came <i>out</i> of it in the next 12 days could be the hero.</p>
    <p><b>1 means linkable.</b> With a handful of friends there isn't enough traffic, and that's honest. It grows with more parcels, longer holds, shared batch days and decoy boxes.</p>
    <p>Sleeves defeat the other attack: the hub before and the hub after never saw the same object, so photos don't link them.</p></div>
</div></section>
<div id="tip" role="tooltip"></div>
</div>
<script>
const D = ${json};
const $ = (s) => document.querySelector(s);
const esc = ${esc.toString()};
$("#heroTitle").textContent = D.hero.title; $("#heroFrom").textContent = D.hero.from; $("#heroTo").textContent = D.hero.to;
$("#seed").textContent = D.seed; $("#hops").textContent = D.hops;
document.querySelectorAll(".tabs button").forEach((b) => b.onclick = () => {
  document.querySelectorAll(".tabs button").forEach((x) => x.setAttribute("aria-selected", x === b));
  document.querySelectorAll(".view").forEach((v) => v.classList.toggle("on", v.id === "v-" + b.dataset.v));
});
const tip = $("#tip");
function hover(el, html) {
  el.addEventListener("mousemove", (e) => { tip.innerHTML = html; tip.style.display = "block"; tip.style.left = e.clientX + 14 + "px"; tip.style.top = e.clientY + 14 + "px"; });
  el.addEventListener("mouseleave", () => tip.style.display = "none");
}
// Equirectangular projection fitted to the hubs.
const lons = D.hubs.map((h) => h.lon), lats = D.hubs.map((h) => h.lat);
const pad = 3, x0 = Math.min(...lons) - pad, x1 = Math.max(...lons) + pad, y0 = Math.min(...lats) - pad, y1 = Math.max(...lats) + pad;
const k = Math.cos((y0 + y1) / 2 * Math.PI / 180);
const sc = Math.min(600 / ((x1 - x0) * k), 420 / (y1 - y0));
const P = (h) => [20 + (h.lon - x0) * k * sc, 440 - (h.lat - y0) * sc];
const hubBy = Object.fromEntries(D.hubs.map((h) => [h.city, h]));
const NS = "http://www.w3.org/2000/svg";
const el = (tag, attrs, parent) => { const e = document.createElementNS(NS, tag); for (const [a, v] of Object.entries(attrs)) e.setAttribute(a, v); parent && parent.appendChild(e); return e; };
function baseMap(svg, { highlight = [], dim = false } = {}) {
  svg.innerHTML = "";
  for (const h of D.hubs) {
    const [x, y] = P(h); const on = highlight.includes(h.city);
    const g = el("g", {}, svg);
    el("circle", { cx: x, cy: y, r: 14, fill: "transparent" }, g); // hit target
    el("circle", { cx: x, cy: y, r: on ? 7 : 5, fill: on ? "var(--series-1)" : "var(--node)", stroke: "var(--surface-1)", "stroke-width": 2, opacity: dim && !on ? 0.4 : 1 }, g);
    const t = el("text", { x: x + 10, y: y + 4 }, g); t.textContent = h.city;
    hover(g, "<b>" + esc(h.city) + "</b> (" + esc(h.country) + ")<br>carries: " + esc(h.accepts));
  }
}
function line(svg, a, b, attrs) { const [x1, y1] = P(hubBy[a]), [x2, y2] = P(hubBy[b]); return el("line", { x1, y1, x2, y2, ...attrs }, svg); }

// Sender view
{
  const svg = $("#mapSender"); baseMap(svg, { highlight: D.hero.path });
  const lines = el("g", {}, svg); svg.insertBefore(lines, svg.firstChild);
  D.hero.path.forEach((c, i) => { if (i) line(lines, D.hero.path[i - 1], c, { stroke: "var(--series-1)", "stroke-width": 2, "stroke-linecap": "round" }); });
  D.hero.path.forEach((c, i) => { const [x, y] = P(hubBy[c]); const t = el("text", { x: x - 4, y: y - 12, style: "font-weight:700;fill:var(--text-primary)" }, svg); t.textContent = i + 1; });
  const steps = D.hero.track; const tl = $("#timeline");
  const name = (i) => i === 0 ? "You → " + D.hero.path[0] : i === D.hero.path.length ? D.hero.path[i - 1] + " → recipient's locker" : D.hero.path[i - 1] + " → " + D.hero.path[i];
  for (const s of steps) {
    const li = document.createElement("li"); if (!s.receipts.length) li.className = "pending";
    const rs = s.receipts.map((r) => r.kind === "refused" ? '<span class="status bad">✕ refused</span> ' + esc(r.reason) : esc(r.kind) + " " + esc(r.day) + (r.shipDay ? " (ships " + esc(r.shipDay) + ")" : "")).join("<br>");
    li.innerHTML = "<b>" + esc(name(s.index)) + "</b><br><span class='muted'>" + (rs || "waiting…") + "</span>";
    tl.appendChild(li);
  }
  $("#packing tbody").innerHTML = D.hero.packing.map((p) => "<tr><td>" + p.sleeve + "</td><td class='mono'>" + esc(p.code) + "</td><td>" + esc(D.hero.path[p.sleeve - 1]) + "</td></tr>").join("");
  $("#qr").innerHTML = D.hero.labelQr; $("#qr").firstElementChild.classList.add("qr");
  $("#labelText").textContent = D.hero.label.slice(0, 64) + "… (" + D.hero.label.length + " chars)";
  $("#labels tbody").innerHTML = D.hero.labels.map((l) => "<tr><td>" + esc(l.by) + "</td><td class='mono'>" + esc(l.head) + "…</td></tr>").join("");
}
// Hub view
{
  const sel = $("#hubSel"); sel.innerHTML = D.hubs.map((h) => "<option>" + esc(h.city) + "</option>").join("");
  sel.value = D.hero.path[1] || D.hero.path[0];
  const draw = () => {
    const c = sel.value, obs = D.observations[c] || [];
    const nb = new Set(obs.flatMap((o) => [o.prev, o.next]).filter((x) => hubBy[x]));
    const svg = $("#mapHub"); baseMap(svg, { highlight: [c, ...nb], dim: true });
    const g = el("g", {}, svg); svg.insertBefore(g, svg.firstChild);
    for (const o of obs) {
      if (hubBy[o.prev]) line(g, o.prev, c, { stroke: "var(--edge)", "stroke-width": 2 });
      if (hubBy[o.next]) line(g, c, o.next, { stroke: "var(--series-1)", "stroke-width": 2, "stroke-dasharray": "4 3" });
    }
    $("#hubTable tbody").innerHTML = obs.length ? obs.map((o) => "<tr><td>" + esc(o.in) + "</td><td>" + esc(o.prev) + "</td><td>" + esc(o.out || "—") + "</td><td>" + esc(o.action === "refuse" ? "refused" : o.next) + "</td><td>" + esc(o.category) + "</td></tr>").join("") : "<tr><td colspan=5 class=muted>No parcels passed through this hub.</td></tr>";
  };
  sel.onchange = draw; draw();
}
// Public view
{
  const svg = $("#mapPublic"); baseMap(svg);
  const g = el("g", {}, svg); svg.insertBefore(g, svg.firstChild);
  const seen = new Set();
  for (const gr of D.grants) { const key = [gr.from, gr.to].sort().join("|"); if (seen.has(key)) continue; seen.add(key); line(g, gr.from, gr.to, { stroke: "var(--edge)", "stroke-width": 1 }); }
  const days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
  $("#pubTable tbody").innerHTML = D.hubs.map((h) => "<tr><td>" + esc(h.city) + "</td><td>" + esc(h.accepts) + "</td><td>" + h.batchDays.map((d) => days[d]).join(", ") + "</td></tr>").join("");
  $("#parcelTable tbody").innerHTML = D.parcels.map((p) => "<tr><td>" + esc(p.title) + (p.id === "hero" ? " <b>(hero)</b>" : "") + "</td><td>" + esc(p.from) + " → " + esc(p.to) + "</td><td>" +
    (p.error ? '<span class="status bad">✕ unroutable</span> <span class="muted">' + esc(p.error) + "</span>" : p.refused ? '<span class="status bad">✕ refused</span> <span class="muted">' + esc(p.refused) + "</span>" : p.delivered ? '<span class="status ok">✓ delivered</span>' : "in transit") + "</td></tr>").join("");
}
// Adversary view: one series, so no legend; the title names it.
{
  const svg = $("#anon"); const A = D.adversary; const max = Math.max(4, ...A.map((a) => a.set || 0));
  const L = 120, W = 440, rowH = 300 / Math.max(A.length, 1);
  for (let v = 0; v <= max; v += Math.ceil(max / 4)) { const x = L + (v / max) * W; el("line", { x1: x, y1: 0, x2: x, y2: 290, stroke: "var(--grid)" }, svg); const t = el("text", { x, y: 300, "text-anchor": "middle" }, svg); t.textContent = v; }
  A.forEach((a, i) => {
    const y = i * rowH + rowH * 0.25, h = Math.min(28, rowH * 0.5), w = Math.max(2, ((a.set || 0) / max) * W);
    const t = el("text", { x: L - 8, y: y + h / 2 + 4, "text-anchor": "end", style: "fill:var(--text-primary)" }, svg); t.textContent = a.city;
    const g = el("g", {}, svg);
    el("rect", { x: L, y: y - 6, width: W, height: h + 12, fill: "transparent" }, g);
    el("path", { d: "M" + L + "," + y + "h" + (w - 4) + "a4,4 0 0 1 4,4v" + (h - 8) + "a4,4 0 0 1 -4,4h" + -(w - 4) + "z", fill: "var(--series-1)" }, g);
    const v = el("text", { x: L + w + 6, y: y + h / 2 + 4, style: "fill:var(--text-primary)" }, svg); v.textContent = a.set + (a.set <= 1 ? "  linkable" : "");
    hover(g, "<b>Honest: " + esc(a.city) + "</b><br>The hero hides among " + a.set + " box(es)");
  });
  $("#anonTable tbody").innerHTML = A.map((a) => "<tr><td>" + esc(a.city) + "</td><td>" + a.set + "</td></tr>").join("");
}
</script></body></html>`;
}
