#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include tests/isolated_capture/t19_runtime_test.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/t19_runtime_test"
"$out/t19_runtime_test"
