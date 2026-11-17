#!/bin/sh
# Usage: ./native/examples/native_surface/run.sh [shared-library-directory] [output-directory]
# The supplied DSO must be the delivery DSO, not extra private objects.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
lib=$(realpath "${1:-$root/build}")
out=${2:-$root/build/native-surface-consumer}
mkdir -p "$out"
out=$(realpath "$out")
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -I"$root/native/include" \
    "$root/native/tests/test_native_surface.c" -L"$lib" -Wl,-rpath,"$lib" \
    -laether_core -pthread -o "$out/consumer"
"$out/consumer" "$out/surface.ppm"
sha256sum "$lib/libaether_core.so" "$root/native/include/aether/aether.h" \
    "$root/native/tests/test_native_surface.c" "$out/consumer" "$out/surface.ppm" > "$out/SHA256SUMS"
