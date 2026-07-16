#!/usr/bin/env bash
# Compile and run every Ember 32 reference + fast-core gate. Exits non-zero on the
# first failure. Used by CI and locally:  cd engines/ember32 && ./run_tests.sh
#
# What it gates:
#   test_features  — the golden feature suite (compositor + ARM/Thumb CPU), 9/9
#   fastcore_test  — the OPTIMISED compositor is pixel-identical to the reference
#   exception_test — banked modes / exceptions / IRQ-FIQ / exact bus timing
#   audio_dsp_test — IMA ADPCM / echo-reverb / streamed channels
#   thumb_test     — Thumb ISA + ARM<->Thumb interworking
#   facade_test    — the FamemuCoreAPI C ABI facade
#   make_cart + cart_file_test — a real .e32 file loaded through the facade
set -euo pipefail
cd "$(dirname "$0")"
CXX="${CXX:-c++}"
FLAGS=(-std=c++17 -O2 -I..)
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

run() { echo "== $1 =="; "$CXX" "${FLAGS[@]}" "tools/$1.cpp" "${@:2}" -o "$TMP/$1"; "$TMP/$1"; }

run test_features
run fastcore_test
run exception_test
run audio_dsp_test
run thumb_test
run facade_test famemu_ember32_core.cpp

echo "== make_cart + cart_file_test =="
"$CXX" "${FLAGS[@]}" tools/make_cart.cpp -o "$TMP/mk"; "$TMP/mk" "$TMP/demo.e32"
"$CXX" "${FLAGS[@]}" tools/cart_file_test.cpp famemu_ember32_core.cpp -o "$TMP/cf"; "$TMP/cf" "$TMP/demo.e32"

echo "ALL EMBER32 GATES PASSED"
