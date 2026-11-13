#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/t19_context_test.cpp tests/isolated_capture/t17_test_issuer.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/t19_context_test"
"$out/t19_context_test"
# This comparison helper adds no native shaping or issuer to the normal binary.
if nm -C "$out/aether_capture" | grep -q 'SyntheticTextTestIssuer::install'; then
 echo 'FAIL: test issuer leaked into normal executable' >&2
 exit 1
fi
echo 'T1.9 context isolation PASS normal issuer ABSENT'
