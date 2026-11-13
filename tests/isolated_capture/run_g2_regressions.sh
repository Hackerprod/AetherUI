#!/bin/sh
# Manual focused raster regressions, no Actions or external runtime.
set -eu
out=${1:?output directory}
mkdir -p "$out"
for name in renderer stroke rounded rounded_stroke curved_clip shadow border_group opacity scroll; do
  c++ -std=c++20 -O1 -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -Inative/include -Inative/src/rendering/preparation/include \
    "native/tests/test_software_${name}.cpp" native/src/rendering/border_painter.cpp \
    native/src/css/border_radii.cpp native/src/css/css_length.cpp \
    native/src/rendering/native_frame.cpp native/src/rendering/preparation/paint_snapshot.cpp \
    native/src/rendering/software/software_renderer.cpp -pthread -o "$out/$name"
  "$out/$name"
  echo "REGRESSION_PASS:$name"
done
c++ -std=c++20 -O1 -Inative/include -Inative/src/rendering/preparation/include \
  native/tests/test_synthetic_a8.cpp native/src/rendering/native_frame.cpp \
  native/src/rendering/preparation/paint_snapshot.cpp \
  native/src/rendering/software/software_renderer.cpp -pthread -o "$out/synthetic_a8"
"$out/synthetic_a8"
echo REGRESSION_PASS:synthetic_a8
