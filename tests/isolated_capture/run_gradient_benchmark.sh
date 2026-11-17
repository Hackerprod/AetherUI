#!/bin/sh
set -eu
# Use this same driver for base and candidate, in sequence under idle conditions.
# Example: candidate/tests/isolated_capture/run_gradient_benchmark.sh BASE NEW_OUT
repo=$(cd "$1" && pwd)
mkdir -p "$2"
out=$(cd "$2" && pwd)
source_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$repo"
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include -Inative/src/rendering/software "$source_dir/gradient_benchmark.cpp" native/src/rendering/native_frame.cpp native/src/rendering/preparation/paint_snapshot.cpp native/src/rendering/software/software_renderer.cpp -pthread -o "$out/benchmark" > "$out/compile.log" 2>&1
"$out/benchmark" > "$out/samples.csv"
