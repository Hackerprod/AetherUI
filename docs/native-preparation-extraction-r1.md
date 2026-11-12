# R1 native preparation extraction

Mechanical extraction from R0 585964e2fd7e6e690356233af66b8a3865c8944c.
New component: native/src/rendering/preparation. Three implementation files and
three headers now use aether::rendering::preparation. This is reusable internal
C++ API, not installed public API or a stable C/binary ABI. The old headers are
namespace-alias shims; existing ordinary calls retain source compatibility, but
clients cannot reopen avc::preparation as a namespace. Rebuild all consumers.

The native target has no core linkage or test/parser/graphics dependencies. ABI
headers supply types only; AetherApi callbacks are supplied by the caller. No
AETHER_EXPORTS or dynamic loading is added. The three old implementation files
are removed, leaving a single implementation. The valider target retains its
name as an INTERFACE compatibility target.

Metrics move with preparation because their two phases describe snapshot_copy
and preflight, not validator reporting, JSON, or capture. They remain optional,
caller-driven and use an injected clock. No implicit timing instrumentation,
real-clock default, engine/raster timing or new CPU/memory metric is introduced.

## Behavior preserved

Exactly 9 supported paint tags, 10 known unsupported tags, unknown-tag errors,
65,536 commands, 256 scope depth, 65,536 bytes per text and 8 MiB string budget.
No missing command is silently ignored. The unchanged adapter restrictions are
not newly asserted as core ABI guarantees. Provider/lista must remain live and
immutable during snapshot. Readable string memory is trusted; bounded scans do
not validate hostile pointers. Successful snapshot owns strings. prepare owns
prepared events. replay borrows events during synchronous callbacks and may
leave the sink partially populated after errors/cancellation; no rollback added.
Provider/cancellation/clock callback nonthrowing contracts are unchanged.

UTF-8 implementation moves byte-for-byte to detail/utf8.hpp as inline function.
The old avc::valid_utf8 signature delegates to it; parser and manifest API stay
in the validator. Contract target gets only a private helper include path even
when preparation option is OFF, not a dependency on the preparation library.

## Parity and build limits

The three sources and headers were compared to the R0 git contents after only
namespace/include substitutions and avc::valid_utf8 -> detail::valid_utf8.
All six match exactly after those substitutions. UTF-8 function body matches
exactly, with inline added. The five original suite files are byte-identical.
No existing test cases were changed.

CMake changes were reviewed statically only: CMake is unavailable in this
environment. Configuring/building targets, option ON/OFF, install/package and
aggregate builds are NOT validated. No workflow was run or changed.

## Manual pure checks

From repository root, without engine/graphics linkage, run in bash:

```bash
set -eu
D=/tmp/aether-r1-tests
mkdir -p "$D"
N=native/src/rendering/preparation
for name in paint_snapshot aether_command_source preparation_metrics; do
 /usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -UNDEBUG \
  -I native/include -I "$N/include" -I tests/visual_compare/include \
  "$N/paint_snapshot.cpp" "$N/aether_command_source.cpp" "$N/preparation_metrics.cpp" \
  tests/visual_compare/tests/test_$name.cpp -o "$D/$name"
 "$D/$name"
done
for name in resource_manifest resource_validation; do
 /usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -UNDEBUG \
  -I "$N/include" -I tests/visual_compare/include -I tests/visual_compare/src \
  tests/visual_compare/src/{cli,resource_manifest,resource_validation,report,artifact_store,sha256}.cpp \
  tests/visual_compare/tests/test_$name.cpp -o "$D/$name"
 "$D/$name"
done
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -UNDEBUG \
 -I native/include -I "$N/include" "$N/paint_snapshot.cpp" \
 "$N/aether_command_source.cpp" "$N/preparation_metrics.cpp" \
 native/tests/test_native_preparation.cpp -o "$D/native_smoke"
"$D/native_smoke"
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -UNDEBUG \
 -I native/include -I "$N/include" -I tests/visual_compare/include \
 tests/visual_compare/tests/test_preparation_compat.cpp -o "$D/compat"
"$D/compat"
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -UNDEBUG -pthread \
 native/src/rendering/native_frame.cpp native/tests/test_native_frame.cpp -o "$D/frame"
"$D/frame"
```

The native smoke compiles without any tests/visual_compare include/source path.
It uses a fake ABI provider, then snapshot -> prepare -> recording sink. This
proves linkage independence and flow, NOT rendering. Original synthetic metrics
remain synthetic. No PNG, platform backend or performance improvement is claimed.

Next stage: connect prepared operations to a candidate frame via a real native
adapter, preserving explicit capability rejection. R0 and R1 remain separate
until that contract is reviewed; no new backend abstraction is frozen here.
