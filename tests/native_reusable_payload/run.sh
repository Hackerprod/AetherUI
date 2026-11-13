#!/bin/sh
set -eu
out=${1:?usage: run.sh NEW_EMPTY_BUILD_DIRECTORY}
sh tests/native_hover_baseline/build.sh "$out"
export TMPDIR="$out/tmp"
c++ -std=c++20 -O1 -Inative/include -Inative/src native/tests/test_reusable_payload.cpp "$out"/core/*.o -pthread -o "$out/payload"
"$out/payload"
