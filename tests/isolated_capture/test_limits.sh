#!/bin/sh
# Tests the same supervisor and setrlimit calls, using a separate test-only binary.
set -eu
probe=${1:?aether_capture_limit_probes}; out=${2:?NEW_RESULTS_DIRECTORY}
mkdir "$out"
for kind in MEMORY CPU WALL; do
 if "$probe" --geometry "${kind}_PROBE" "$out/$kind" 1 1 1; then echo "probe unexpectedly succeeded $kind"; exit 1; fi
 test ! -e "$out/$kind/aether.png"
done
grep -q 'worker_exit=42' "$out/MEMORY/report.log"
grep -q 'worker_signal=9' "$out/CPU/report.log"
grep -q 'supervisor_status=TIMEOUT' "$out/WALL/report.log"
echo 'PASS real address-space rejection, CPU signal, supervisor wall kill'
