#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/t17_context_layout_tests.cpp tests/isolated_capture/t17_test_issuer.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/t17_context_layout_tests"
"$out/t17_context_layout_tests"
# The normal target is linked without the sole issuer implementation.
if nm -C "$out/aether_capture" | grep -q 'SyntheticTextTestIssuer::install'; then
 echo 'FAIL: test issuer leaked into normal executable' >&2
 exit 1
fi
nm -C "$out/t17_context_layout_tests" | grep ' T .*SyntheticTextTestIssuer::install' >/dev/null
echo 'T1.7 link isolation PASS normal issuer ABSENT; test issuer PRESENT'
