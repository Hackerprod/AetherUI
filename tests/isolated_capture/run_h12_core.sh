#!/bin/sh
# Manual test-only sweep; no Actions. Core objects are linked directly so the
# allocation injector intercepts allocations in the engine as well as the test.
set -eu
out=${1:?build directory}
c++ -std=c++20 -O1 -Wall -Wextra -Inative/include native/tests/test_flex_static_position_lifetime.cpp "$out"/core/*.o -pthread -o "$out/h12_lifetime"
"$out/h12_lifetime"
python3 - "$out/h12_lifetime" <<'PY'
import resource, subprocess, sys
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
for mode in ('--negative-order', '--negative-geometry'):
    run = subprocess.run([sys.argv[1], mode], capture_output=True, text=True)
    assert run.returncode != 0, (mode, 'oracle accepted corruption')
    assert 'FAIL prepared flex-static lifetime' in run.stderr, (mode, run.stderr)
    print('PASS negative oracle:', mode)
PY
