#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pedantic \
  "$root/native/src/css/eligibility/capture_gate.cpp" \
  "$root/native/tests/test_css_eligibility_barrier.cpp" -o "$out/g12"
"$out/g12"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  "$root/native/src/css/eligibility/capture_gate.cpp" \
  "$root/native/tests/test_css_eligibility_barrier.cpp" -o "$out/g12-ubsan"
"$out/g12-ubsan"
printf '%s\n' 'G1.2 barrier strict/UBSan PASS; 64 combinations each; authored eligibility remains UNKNOWN'
