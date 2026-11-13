#!/bin/sh
# Manual Linux-only clean core build and cache regression/benchmark. No Actions.
set -eu
out=${1:?usage: tests/hit_order_cache/run.sh NEW_BUILD_DIRECTORY}
mkdir -p "$out/core"
awk '/add_library\(aether_core SHARED/{on=1;next}on && /\)/{exit}on && /\.cpp/{print "native/"$1}' native/CMakeLists.txt > "$out/sources.txt"
while IFS= read -r source; do
 object=$(printf '%s' "$source" | tr / _)
 c++ -std=c++20 -O1 -Inative/include -c "$source" -o "$out/core/$object.o"
done < "$out/sources.txt"
c++ -std=c++20 -O1 -Inative/include native/tests/test_hit_order_cache.cpp "$out"/core/*.o -pthread -o "$out/test_hit_order_cache"
"$out/test_hit_order_cache" | tee "$out/report.log"
