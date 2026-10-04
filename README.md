# MuleNet

> **Not endorsed by or affiliated with Logos.** MuleNet is an independent experiment, built on the
> Logos tech stack. It is untested with real parcels; see Status below.

**A physical mixnet.** A parcel hops through a chain of friends' hubs so that nobody, not the
hubs, not the carriers, not an observer, can link the sender to the recipient. Hubs are kept as
safe as we know how: they never open a parcel, never publish their address, and only ever learn
the hub before and the hub after them.

It's the onion routing idea applied to boxes:

| mixnet | MuleNet |
|---|---|
| onion packet | a fixed-size **onion label** (QR) - each hub peels one layer and learns only where to ship next |
| re-encryption per hop | **re-boxing** per hop - the old box and label never travel on |
| layered payload | **sleeves** - the item is sealed in one tamper-evident sleeve per hop; hub *i* removes sleeve *i* |
| fixed packet size | standard **box classes** and **weight bands** - hubs pad every box to the band |
| mixing delay | random **hold days** + shared **batch days** |
| exit node | the recipient's **mailbox** - a pickup point only the exit hub can read |
| delivery receipts | **custody receipts** over Logos Messaging, readable only by the neighbours and the sender |

Built on the Logos tech stack: a Basecamp module (`mulenet_core` + the `mulenet` view) that syncs a
signed hub directory and the receipts over [Loam](https://github.com/vpavlin/loam-basecamp)
(Logos Delivery), with the [loam-sync](https://github.com/vpavlin/loam-sync) event log + RBSR
catch-up. Payments and bonds on LEZ come later (docs/ROADMAP.md).

## Status (0.1.0, 2026-10-04)

- **Works end to end** on the live Logos test fleet: two headless `mulenet_core` peers and a real
  Basecamp 0.2.0 synced the directory, sealed and opened address grants, planned a route, relayed
  a parcel, and delivered every custody receipt.
- **Desktop (Basecamp):** `mulenet_core` + `mulenet` view, both build as portable `.lgx`.
- **Android hub app:** not started (next; the hub flow is the phone's job: scan, peel, print).
- **Not yet used with real parcels.** The first real run is a 3D-printed item through a few
  friends; see docs/HUB-GUIDE.md.
- Website (draft): https://vpavlin.github.io/mulenet/ (source in site/).
- Contents inspection is deliberately **not** part of the design yet: docs/OPEN-PROBLEMS.md.

## Layout

```
packages/core/     JS reference implementation (label, sealing, receipts, directory, planner, hub flow)
mulenet_core/      the Basecamp core module (C++, byte-identical to packages/core; Qt-free engine)
module/            the Basecamp ui_qml view (pure QML over mulenet_core)
sim/               day-by-day simulator with a dev visualization (sim/out/index.html)
hub/               mn.py - drive headless mulenet_core peers under logos-hub
scripts/qml-harness/  renders the real view against the real core, offscreen
vendor/loam-sync/  git submodule (the JS side imports it; C++ headers are copied into mulenet_core/src/logos_sync)
docs/              SPEC, THREAT-MODEL, OPEN-PROBLEMS, HUB-GUIDE, ROADMAP, adr/
```

## Try it

```sh
git submodule update --init
npm install
npm test                                  # JS core: 15 tests
mulenet_core/test/run-tests.sh            # C++: 42 parity checks vs JS, 21 engine, 21 e2e (5 instances)
npm run sim                               # simulator -> sim/out/index.html
scripts/qml-harness/render.sh             # screenshots of every view tab against the real core

(cd mulenet_core && nix build .#lgx-portable)
(cd module && nix build .#lgx-portable)
```

Install in Basecamp from a package repo that also carries `loam_core` and its dependencies
(`delivery_module`, `ble_mesh`, `keycard`); the public vpavlin catalog has those.

## How a parcel travels

1. **The recipient** creates a mailbox at an exit hub (pickup point sealed to that hub) and gives
   the sender the mailbox card `MNBOX1.<ref>.<exit>`. The sender never sees the pickup point.
2. **The sender** plans a route (random, policy-matching, vouched hubs). MuleNet prints the label
   and a packing list: the item in its plain inner bag, then one sealed sleeve per hop.
3. **Each hub** scans the label, types the outer sleeve's seal code and weighs the box. MuleNet
   checks the seal, peels the layer and says: remove sleeve X, re-box in class S, pad Y g, ship by
   Thursday to this address, print this new label.
4. **Receipts** ("received", "shipped", "delivered", "ready") flow back over Logos Messaging. Only
   the sender (and the neighbouring hubs, for their own custody) can read them.

The full protocol is docs/SPEC.md; why each piece is the way it is lives in docs/adr/.

## License

MIT or Apache-2.0.
