#!/bin/sh
set -eu
out=${1:?clean build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/g1_gradient_tests.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/g1_gradient_tests"
"$out/g1_gradient_tests"
