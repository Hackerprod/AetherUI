#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/h18_core_tests.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/h18_core_tests"
"$out/h18_core_tests"
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/h18_gate_tests.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/h18_gate_tests"
"$out/h18_gate_tests"
