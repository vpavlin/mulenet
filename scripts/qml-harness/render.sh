#!/usr/bin/env bash
# Render the MuleNet view offscreen against the real core (system Qt6 Quick).
# Design system: the one bundled in the target Basecamp (MN_DS), else a checkout.
#   scripts/qml-harness/render.sh            -> screenshots in ${MN_OUT:-/tmp/mulenet-harness}
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HDIR="$ROOT/scripts/qml-harness"
OUT="${MN_OUT:-/tmp/mulenet-harness}"
mkdir -p "$OUT"
DS="${MN_DS:-$HOME/basecamp-test/squashfs-root/usr/lib}"
[ -f "$DS/Logos/Theme/qmldir" ] || { echo "design system not found at $DS (set MN_DS)" >&2; exit 2; }
MOC="$(pkg-config --variable=libexecdir Qt6Core)/moc"
"$MOC" "$HDIR/harness.cpp" -o "$HDIR/harness.moc"
g++ -std=c++20 -O1 -fPIC -Wno-deprecated-declarations -I"$HDIR" -I"$ROOT/mulenet_core/test/fakesdk" -I"$ROOT/mulenet_core/src" \
    $(pkg-config --cflags Qt6Quick Qt6Qml Qt6Gui Qt6Core) "$HDIR/harness.cpp" "$ROOT/mulenet_core/src/mulenet_core_impl.cpp" \
    "$ROOT/mulenet_core/src/qrcodegen.cpp" $(pkg-config --libs Qt6Quick Qt6Qml Qt6Gui Qt6Core) -lcrypto -o "$OUT/harness"
QML_IMPORT_PATH="$DS" "$OUT/harness" "$ROOT/module/Main.qml" "$OUT"
