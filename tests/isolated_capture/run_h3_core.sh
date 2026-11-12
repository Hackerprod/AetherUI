#!/bin/sh
# Run after build.sh has built ALL current core sources. No Actions.
set -eu
out=${1:?usage: run_h3_core.sh BUILD_DIRECTORY}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/h3_core.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/h3_core"
"$out/h3_core"
