#!/bin/sh
# Run from repo root after build.sh. Baseline differs ONLY in eager scope walk.
set -eu
build=${1:?candidate build directory}
out=${2:?new benchmark evidence directory}
mkdir -p "$out"
{
 c++ --version | head -n 1
 printf 'flags=-std=c++20 -O1\narchitecture='
 uname -m
 sha256sum native/src/style_runtime/style_runtime_core.cpp tests/isolated_capture/t19_scope_benchmark.cpp "$build/core/native_src_style_runtime_style_runtime_core.cpp.o"
} > "$out/build-metadata.txt"
python3 - "$out/eager_core.cpp" <<'PY'
import sys
s=open('native/src/style_runtime/style_runtime_core.cpp').read()
a=s.index('    // T19_SCOPE_BEGIN:')
b=s.index('    // T19_SCOPE_END',a)+len('    // T19_SCOPE_END')
s=s[:a]+'''    auto* scope_counters = typography_scope_counters;
    if (scope_counters) { ++scope_counters->node_computations; ++scope_counters->queries; }
    bool typography_scope_result = false;
    for (auto* current=scope.get(); current; current=current->parent()) {
        if (scope_counters) ++scope_counters->scopes;
        for (const auto& terminal:current->local_entries()) {
            if (scope_counters) ++scope_counters->terminals;
            if (terminal.status()!=custom_property_status::valid) typography_scope_result=true;
        }
    }
    auto typography_scope_uncertain = [&]() { return typography_scope_result; };
'''+s[b:]
open(sys.argv[1],'w').write(s)
PY
c++ -std=c++20 -O1 -Inative/include -Inative/src/style_runtime -c "$out/eager_core.cpp" -o "$out/eager_core.o"
c++ -std=c++20 -O1 -Inative/include -c tests/isolated_capture/t19_scope_benchmark.cpp -o "$out/benchmark.o"
# Exclude exactly the core translation unit replaced above.
find "$build/core" -name '*.o' ! -name 'native_src_style_runtime_style_runtime_core.cpp.o' | sort > "$out/shared_objects.txt"
c++ "$out/benchmark.o" "$build"/core/*.o "$build"/extra/*.o -pthread -o "$out/candidate"
c++ "$out/benchmark.o" "$out/eager_core.o" $(cat "$out/shared_objects.txt") "$build"/extra/*.o -pthread -o "$out/baseline"
"$out/baseline" 7 > "$out/baseline.csv"
"$out/candidate" 7 > "$out/candidate.csv"
python3 tests/isolated_capture/t19_scope_summarize.py "$out"
