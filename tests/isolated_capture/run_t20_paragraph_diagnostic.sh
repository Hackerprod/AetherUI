#!/bin/sh
set -eu
out=${1:?build directory}
c++ -std=c++20 -Wall -Wextra -Werror tests/isolated_capture/t20_paragraph_diagnostic_test.cpp -o "$out/t20_paragraph_diagnostic_test"
"$out/t20_paragraph_diagnostic_test"
