#!/bin/sh
# Manual Linux-only build; no Actions or automatic test execution.
set -eu
out=${1:?usage: build.sh NEW_BUILD_DIRECTORY}
mkdir -p "$out/core"
awk '/add_library\(aether_core SHARED/{on=1;next}on && /\)/{exit}on && /\.cpp/{print "native/"$1}' native/CMakeLists.txt > "$out/sources.txt"
while IFS= read -r source; do
 object=$(printf '%s' "$source" | tr / _)
 c++ -std=c++20 -O1 -Inative/include -c "$source" -o "$out/core/$object.o"
done < "$out/sources.txt"
mkdir -p "$out/extra"
for source in native/src/rendering/native_frame.cpp native/src/rendering/preparation/paint_snapshot.cpp native/src/rendering/software/software_renderer.cpp native/src/rendering/preparation/aether_command_source.cpp native/src/rendering/preparation/native_command_source.cpp tests/visual_compare/src/sha256.cpp tests/isolated_capture/h7_profile.cpp tests/isolated_capture/h11_profile.cpp tests/isolated_capture/h19_profile.cpp; do
 object=$(printf '%s' "$source" | tr / _)
 c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include -c "$source" -o "$out/extra/$object.o"
done
for mode in normal probes; do
 flag=; suffix=
 if [ "$mode" = probes ]; then flag=-DH1_TEST_LIMIT_PROBES; suffix=_limit_probes; fi
 c++ -std=c++20 -O1 -Wall -Wextra $flag -Inative/include -Inative/src/rendering/preparation/include -c tests/isolated_capture/main.cpp -o "$out/main$suffix.o"
 c++ "$out/main$suffix.o" "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/aether_capture$suffix"
done
