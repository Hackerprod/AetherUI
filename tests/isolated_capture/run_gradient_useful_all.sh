#!/bin/sh
# Run manually from a fresh checkout root; never enables CI/Actions.
set -eu
out=${1:?absolute NEW output directory}
case "$out" in /*) ;; *) echo 'output must be absolute' >&2;exit 2;; esac
if [ -e "$out" ]; then echo 'output directory must not exist' >&2;exit 2;fi
mkdir -p "$out"
git rev-parse HEAD > "$out/base-sha.txt"
git status --short > "$out/git-status.txt"
git diff HEAD > "$out/candidate.patch"
c++ --version > "$out/compiler.txt"
sha256sum native/src/rendering/software/software_renderer.cpp tests/isolated_capture/*gradient* > "$out/input-hashes.txt"
# Each attempted stage retains its own log and exact exit status. A failure stops
# dependent work; missing stage entries mean NOT_RUN, never an implicit PASS.
stage() {
 name=$1;shift
 if "$@" > "$out/$name.log" 2>&1; then
  printf '%s\tPASS\t0\n' "$name" >> "$out/stages.tsv"
 else
  status=$?;printf '%s\tFAIL\t%s\n' "$name" "$status" >> "$out/stages.tsv";exit "$status"
 fi
}
stage build sh tests/isolated_capture/build.sh "$out/build"
stage g1 sh tests/isolated_capture/run_g1_gradient.sh "$out/build"
stage g1-focal sh tests/isolated_capture/run_g1_focal.sh "$out/focal"
stage g1-native sh tests/isolated_capture/run_g1_native.sh "$out/build"
stage classifier-build c++ -std=c++20 -O1 -Wall -Wextra -Inative/include -Inative/src/rendering/preparation/include tests/isolated_capture/gradient_classifier_tests.cpp native/src/rendering/native_frame.cpp native/src/rendering/preparation/paint_snapshot.cpp -pthread -o "$out/classifier"
stage classifier "$out/classifier"
stage useful sh tests/isolated_capture/run_gradient_useful.sh "$out/useful"
stage html-original python3 tests/isolated_capture/test_g1_gradient.py "$out/build/aether_capture" "$out/html-original"
stage html-useful python3 tests/isolated_capture/test_gradient_useful.py "$out/build/aether_capture" "$out/html-useful"
stage regressions sh tests/isolated_capture/run_g2_regressions.sh "$out/regressions"
stage translations sh tests/isolated_capture/run_g2_focal.sh "$out/translations"
