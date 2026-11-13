#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include tests/isolated_capture/h19_gate_tests.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/h19_gate_tests"
"$out/h19_gate_tests"
