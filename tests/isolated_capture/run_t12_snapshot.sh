#!/bin/sh
set -eu
out=${1:?clean build directory}
c++ -std=c++20 -O1 -Inative/include -Inative/src/rendering/preparation/include native/tests/test_native_text_snapshot.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/t12_snapshot_tests"
"$out/t12_snapshot_tests"
