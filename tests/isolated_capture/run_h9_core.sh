#!/bin/sh
set -eu
out=${1:?build directory}
for test in h9_core_tests h9_gate_tests h9_oom_tests h9_runtime_oom_tests; do
 c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/$test.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/$test"
 "$out/$test"
done
