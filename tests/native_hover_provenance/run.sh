#!/bin/sh
set -eu
out=${1:?usage: run.sh NEW_EMPTY_BUILD_DIRECTORY}
sh tests/native_hover_baseline/build.sh "$out"
export TMPDIR="$out/tmp"
c++ -std=c++20 -O1 -Inative/include -Inative/src native/tests/test_hover_provenance.cpp "$out"/core/*.o -pthread -o "$out/provenance"
"$out/provenance"
c++ -std=c++20 -O1 -Inative/include -Inative/src -c native/src/style_runtime/style_runtime_core.cpp -o "$out/runtime.o"
c++ -std=c++20 -O1 -Inative/include -Inative/src native/tests/test_style_revisions_atomic.cpp "$out"/core/*.o "$out/runtime.o" -pthread -o "$out/revisions"
"$out/revisions"
