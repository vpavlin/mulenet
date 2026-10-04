#!/usr/bin/env bash
# Build + run the C++ tests without nix (system g++, OpenSSL, nlohmann-json, Qt6Core).
#   mulenet_core/test/run-tests.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${MN_TEST_OUT:-${TMPDIR:-/tmp}/mulenet-ctest}"
mkdir -p "$OUT"
CXX="g++ -std=c++20 -O1 -g -Wall -Wno-deprecated-declarations -I$HERE/../src"
$CXX "$HERE/parity_test.cpp" -lcrypto -o "$OUT/parity_test"
"$OUT/parity_test" "$HERE/vectors.json"
$CXX "$HERE/engine_test.cpp" -lcrypto -o "$OUT/engine_test"
"$OUT/engine_test" "$HERE/vectors.json"
QTINC=$(pkg-config --cflags Qt6Core)
QTLIB=$(pkg-config --libs Qt6Core)
$CXX -fPIC -I"$HERE/fakesdk" $QTINC "$HERE/e2e_test.cpp" "$HERE/../src/mulenet_core_impl.cpp" "$HERE/../src/qrcodegen.cpp" $QTLIB -lcrypto -o "$OUT/e2e_test"
"$OUT/e2e_test"
