#!/bin/sh
set -eu
out=${1:?clean build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include native/tests/test_synthetic_a8.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/t13_a8_tests"
"$out/t13_a8_tests" "$out"
