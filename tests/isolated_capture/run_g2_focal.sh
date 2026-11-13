#!/bin/sh
# Native ABI-only tests. No Chromium, HTML, CMake, fonts or Actions.
set -eu
out=${1:?output directory}
mkdir -p "$out"
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/g2_translation_tests.cpp native/src/rendering/native_frame.cpp native/src/rendering/preparation/paint_snapshot.cpp native/src/rendering/software/software_renderer.cpp -pthread -o "$out/g2_translation_tests"
"$out/g2_translation_tests"
