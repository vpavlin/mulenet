# Headless peers

`mulenet_core` runs headless under logoscore (via [logos-hub](https://github.com/vpavlin/logos-hub)),
e.g. as an always-on directory peer or for tests.

1. Unpack portable `.lgx` files for `mulenet_core`, `loam_core`, `delivery_module`, `ble_mesh`,
   `keycard` into a modules dir (one folder per module: the `variants/linux-amd64` files +
   `manifest.json` + a `variant` file containing `linux-amd64`).
2. A logos-hub profile:

```json
{ "name": "mn-a", "modulesDir": "/path/to/modules", "load": ["mulenet_core"],
  "env": { "QT_QPA_PLATFORM": "offscreen", "EMIT_FROM_THREAD": "1", "MULENET_CORE_DATA": "{data}" } }
```

3. `logos-hub up mn-a`, then drive it: `hub/mn.py mn-a snapshot`, `hub/mn.py mn-a setupHub --cardJson '{...}'`.

Gotcha: logoscore's CLI turns a bare `0x...` argument into a number. `mn.py` wraps those in quotes;
the core strips them.

Env: `MULENET_CORE_DATA` (data dir), `MULENET_DELIVERY_CFG` (delivery config JSON override),
`MULENET_NO_RECEIPT_DELAY` (post receipts immediately - tests and demos only), `MULENET_TICK_MS`.
