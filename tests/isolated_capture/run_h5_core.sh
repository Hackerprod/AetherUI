#!/bin/sh
set -eu
out=${1:?build directory}
for test in h5_core_tests h5_rgba_tests h5_gate_tests; do
 c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/$test.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/$test"
 "$out/$test"
done
