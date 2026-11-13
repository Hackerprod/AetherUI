#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/src/style_runtime tests/isolated_capture/t18_typography_parser_test.cpp native/src/style_runtime/css_variable_tokens.cpp -o "$out/t18_typography_parser_test"
"$out/t18_typography_parser_test"
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include tests/isolated_capture/t18_typography_runtime_test.cpp "$out"/core/*.o "$out"/extra/*.o -pthread -o "$out/t18_typography_runtime_test"
"$out/t18_typography_runtime_test"
