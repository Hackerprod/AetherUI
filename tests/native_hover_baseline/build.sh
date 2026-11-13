#!/bin/sh
# Clean manual build. No Actions, packages, CMake or network access.
set -eu
out=${1:?usage: build.sh NEW_EMPTY_DIRECTORY}
[ ! -e "$out" ] || { echo 'output must not exist' >&2; exit 2; }
mkdir -p "$out/core" "$out/tmp"
export TMPDIR="$out/tmp"
awk '/add_library\(aether_core SHARED/{on=1;next}on && /\)/{exit}on && /\.cpp/{print "native/"$1}' native/CMakeLists.txt > "$out/sources.txt"
while IFS= read -r source; do
 case "$source" in */style_runtime_core.cpp) continue;; esac
 object=$(printf '%s' "$source" | tr / _)
 c++ -std=c++20 -O1 -Inative/include -Inative/src -c "$source" -o "$out/core/$object.o"
done < "$out/sources.txt"
for mode in 0 1 2; do
 c++ -std=c++20 -O1 -DAETHER_HOVER_PROBE=$mode -Inative/include -Inative/src native/tests/test_native_hover_baseline.cpp "$out"/core/*.o -pthread -o "$out/hover-$mode"
done
