#!/bin/sh
set -eu
out=${1:?build directory}
for test in h7_animation_parser_tests h7_core_tests h7_gate_tests h7_order_tests; do
 c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/$test.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/$test"
 "$out/$test"
done
