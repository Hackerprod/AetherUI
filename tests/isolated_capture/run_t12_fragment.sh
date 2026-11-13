#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/t12_fragment_tests.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/t12_fragment_tests"
"$out/t12_fragment_tests"
