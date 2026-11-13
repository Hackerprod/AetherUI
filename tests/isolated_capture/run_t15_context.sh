#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -Wall -Wextra -Inative/include tests/isolated_capture/t15_context_tests.cpp -o "$out/t15_context_tests"
"$out/t15_context_tests"
