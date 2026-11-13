#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include tests/isolated_capture/t20_runtime_test.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/t20_runtime_test"
"$out/t20_runtime_test"
test -x "$out/aether_capture"
if nm -C "$out/aether_capture" | grep -q 'SyntheticTextTestIssuer::install'; then
 echo 'FAIL: synthetic issuer in normal binary' >&2; exit 1
fi
echo 'T2.0 normal issuer ABSENT'
