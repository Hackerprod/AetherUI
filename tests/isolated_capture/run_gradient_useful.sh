#!/bin/sh
set -eu
out=${1:?new output directory}
mkdir -p "$out"
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/gradient_useful_tests.cpp native/src/rendering/native_frame.cpp native/src/rendering/preparation/paint_snapshot.cpp -pthread -o "$out/gradient_useful_tests"
"$out/gradient_useful_tests"
