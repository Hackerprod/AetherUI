#!/usr/bin/env bash
set -euo pipefail
# Linux white-box tests. Link the actual core objects, never a second registry
# next to the shared library. First build aether_core in the supplied CMake tree.
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=$(cd "${1:?usage: run_style_publication_fault_tests.sh CMAKE_BUILD_DIR [OUTPUT_DIR]}" && pwd)
out=${2:-"$build/publication-fault-tests"}
mkdir -p "$out"
mapfile -d '' objects < <(find "$build/CMakeFiles/aether_core.dir/src" -name '*.o' -print0 | sort -z)
((${#objects[@]} > 0))
for test in test_style_publication_preview test_style_publication_preview_oom; do
    "${CXX:-g++}" -std=c++20 -pthread -DAETHER_TEST_INTERNAL_REGISTRY -I "$repo/native/include" \
        "$repo/native/tests/$test.cpp" "${objects[@]}" -o "$out/$test"
    "$out/$test"
done
