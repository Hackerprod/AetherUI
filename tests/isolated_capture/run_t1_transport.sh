#!/bin/sh
set -eu
out=${1:?clean build directory}
c++ -std=c++20 -O1 -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/t1_transport_tests.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/t1_transport_tests"
"$out/t1_transport_tests"
