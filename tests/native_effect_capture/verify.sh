#!/bin/sh
set -eu
out=${1:?usage: verify.sh EXISTING_CLEAN_BUILD_DIRECTORY}
export TMPDIR="$out/tmp"
for mode in 0 1; do
 flags="-std=c++20 -O1 -DAETHER_REUSABLE_PAYLOAD_CAPTURE=0 -DAETHER_EFFECT_CAPTURE=$mode -Inative/include -Inative/src"
 c++ $flags -c native/src/style_runtime/style_runtime_core.cpp -o "$out/runtime-$mode.o"
 c++ $flags native/tests/test_style_revisions_atomic.cpp "$out"/core/*.o "$out/runtime-$mode.o" -pthread -o "$out/atomic-$mode"
 "$out/atomic-$mode"
 c++ $flags native/tests/test_hover_provenance.cpp "$out"/core/*.o -pthread -o "$out/provenance-$mode"
 "$out/provenance-$mode"
 c++ $flags native/tests/test_native_hover_baseline.cpp "$out"/core/*.o -pthread -o "$out/canonical-$mode"
done
for n in 2 18 100; do
 for sc in 0 1 2 3 4 5 6; do
  "$out/canonical-0" --canonical "$n" "$sc" > "$out/canon-off"
  "$out/canonical-1" --canonical "$n" "$sc" > "$out/canon-on"
  cmp "$out/canon-off" "$out/canon-on"
 done
done
echo 'PASS 21 fixture scenarios: canonical OFF/ON equality'
