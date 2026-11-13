#!/bin/sh
# Run after a clean tests/native_hover_baseline/build.sh build; no external tools.
set -eu
out=${1:?usage: run.sh EXISTING_BUILD_DIRECTORY_WITH_CORE_OBJECTS}
export TMPDIR="$out/tmp"
for mode in 0 1; do
 c++ -std=c++20 -O1 -DAETHER_EFFECT_CAPTURE=$mode -Inative/include -Inative/src native/tests/test_effect_capture.cpp "$out"/core/*.o -pthread -o "$out/effects-$mode"
 "$out/effects-$mode" > "$out/effects-$mode.log"
 cat "$out/effects-$mode.log"
done
grep "^CALLBACK_TRACE " "$out/effects-0.log" > "$out/effects-callbacks-off.txt"
grep "^CALLBACK_TRACE " "$out/effects-1.log" > "$out/effects-callbacks-on.txt"
cmp "$out/effects-callbacks-off.txt" "$out/effects-callbacks-on.txt"
