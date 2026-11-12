#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include tests/isolated_capture/h11_core_tests.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/h11_core_tests"
"$out/h11_core_tests"
