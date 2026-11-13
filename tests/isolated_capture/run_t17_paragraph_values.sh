#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -Wall -Wextra -Inative/include tests/isolated_capture/t17_paragraph_values_tests.cpp -o "$out/t17_paragraph_values_tests"
"$out/t17_paragraph_values_tests"
