#!/bin/sh
set -eu
out=${1:?build output directory}
mkdir -p "$out"
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include tests/isolated_capture/t14_font_source_tests.cpp -pthread -o "$out/t14_font_source_tests"
"$out/t14_font_source_tests"
