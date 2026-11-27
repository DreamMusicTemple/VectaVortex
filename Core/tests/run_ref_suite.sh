#!/bin/sh
# Compares the C++ engine with the real web app's audio for every reference scenario (needs build/vv_ref_compare).
# Reference audio comes from tools/ref_render.js (Chromium); regenerate after changing the web app or scenarios.
cd "$(dirname "$0")"; BIN=${1:-../build/vv_ref_compare}; fail=0; n=0
for f in scenarios/*.json; do
  b=${f%.json}; name=$(basename "$b"); [ -f "scenarios/ref/$name.ref.f32" ] || { echo "skip $name (no reference)"; continue; }
  tol=$(grep "^$name " tolerances.txt 2>/dev/null | cut -d' ' -f2); tol=${tol:-1e-4}
  "$BIN" "$b" ../../Assets/Wavetables "$tol" || { fail=$((fail+1)); echo "  ^ FAILED (tol $tol)"; }; n=$((n+1))
done
echo "reference scenarios: $n run, $fail failed"; [ $fail -eq 0 ]
