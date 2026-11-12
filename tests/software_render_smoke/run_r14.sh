#!/bin/sh
# Manual only. No CI/workflows. Run from repository root with a fresh output dir.
set -eu
out=${1:?usage: run_r14.sh OUTPUT_DIRECTORY}
mkdir -p "$out/core"
inc='-Inative/include -Inative/src/rendering/preparation/include'
base='native/src/rendering/native_frame.cpp native/src/rendering/preparation/paint_snapshot.cpp native/src/rendering/software/software_renderer.cpp'
c++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic $inc $base native/tests/test_software_opacity.cpp -pthread -o "$out/opacity"
"$out/opacity" > "$out/precision.log"
# Core is compiled from the current source tree. New pure raster/capture files use strict flags.
awk '/add_library\(aether_core SHARED/{on=1;next}on && /\)/{exit}on && /\.cpp/{print "native/"$1}' native/CMakeLists.txt > "$out/core-sources.txt"
while IFS= read -r source; do
 object=$(printf '%s' "$source" | tr / _)
 c++ -std=c++20 -O1 -Inative/include -c "$source" -o "$out/core/$object.o"
done < "$out/core-sources.txt"
# Existing measure_state.h emits one enum/non-enum -Wextra warning through
# display_list_types.h. Exception is limited to this integration build.
c++ -std=c++20 -O2 -Wall -Wextra -Werror -Wno-error=extra -pedantic $inc $base \
 native/src/rendering/preparation/aether_command_source.cpp native/src/rendering/preparation/native_command_source.cpp \
 native/tests/test_software_opacity_emitter.cpp "$out"/core/*.o -pthread -o "$out/emitter"
"$out/emitter" "$out" > "$out/emitter.log"
