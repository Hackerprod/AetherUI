#!/bin/sh
# Dependency-free core raster test. Manual only; does not invoke Chromium/Actions.
set -eu
out=${1:?output directory}
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/g1_gradient_tests.cpp native/src/rendering/native_frame.cpp native/src/rendering/preparation/paint_snapshot.cpp native/src/rendering/software/software_renderer.cpp -pthread -o "$out/g1_gradient_tests"
"$out/g1_gradient_tests"
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/g1_gradient_oom_tests.cpp native/src/rendering/native_frame.cpp native/src/rendering/preparation/paint_snapshot.cpp native/src/rendering/software/software_renderer.cpp -pthread -o "$out/g1_gradient_oom_tests"
"$out/g1_gradient_oom_tests"
