#!/usr/bin/env python3
"""Tiny driver for headless mulenet_core peers under logos-hub (testing / demos).

    mn.py <profile> <method> [--flag value ...]   -> prints the method's JSON result
"""
import json, os, subprocess, sys

HUB = os.environ.get("LOGOS_HUB", os.path.expanduser("~/logos-hub/logos-hub"))

def call(profile, method, **kw):
    args = [HUB, "call", profile, "mulenet_core", method]
    for k, v in kw.items():
        v = v if isinstance(v, str) else json.dumps(v)
        # logoscore's CLI types a bare 0x... token as a NUMBER; quote it (the core unquotes).
        if v.startswith("0x"): v = '"' + v + '"'
        args += [f"--{k}", v]
    out = subprocess.run(args, capture_output=True, text=True).stdout
    try:
        r = json.loads(out).get("result")
    except Exception:
        return {"ok": False, "error": "no JSON from logos-hub", "raw": out[-300:]}
    for _ in range(2):
        if isinstance(r, str):
            try: r = json.loads(r)
            except Exception: break
    return r

if __name__ == "__main__":
    prof, method, rest = sys.argv[1], sys.argv[2], sys.argv[3:]
    kw = {rest[i][2:]: rest[i + 1] for i in range(0, len(rest), 2)}
    print(json.dumps(call(prof, method, **kw), indent=1))
