# Reproduce D2 in an isolated checkout

Product source `0d715cfde53e03b84c08ae75ed56eec39608a2f6`. Do not replace the native library solely through MSBuild PreserveNewest.

```sh
set -euo pipefail
CMAKE=/workspace/shared/aetherui-password-recovery-20261108/tools/cmake/cmake/data/bin/cmake
CTEST=${CMAKE%/*}/ctest
DOTNET=/workspace/shared/aetherui-dotnet-sdk-20261109/sdk/dotnet
export DOTNET_CLI_HOME=/tmp/aether-dotnet-home DOTNET_CLI_TELEMETRY_OPTOUT=1

$CMAKE -S native -B ../build-context -DCMAKE_BUILD_TYPE=Release \
  -DAETHERUI_BUILD_TESTS=ON -DAETHERUI_ENABLE_STATE_INCREMENTAL=ON \
  -DAETHERUI_ENABLE_CONTEXT_OWNERS=ON -DAETHERUI_ENABLE_WORK_COUNTERS=ON \
  -DAETHERUI_DOTNET_EXECUTABLE="$DOTNET"
$CMAKE --build ../build-context -j4
mkdir -p artifacts/native/linux-x64/lib
cp -L ../build-context/libaether_core.so artifacts/native/linux-x64/lib/libaether_core.so
$CTEST --test-dir ../build-context --output-on-failure -j4
# Focused registered gates, not a substitute for the full run:
$CTEST --test-dir ../build-context --output-on-failure -R '^aether\.(effective-context|generated-context)'
# Managed-only runner also copies/verifies the exact library:
bindings/dotnet/tests/run-context-owner-tests.sh "$DOTNET" ../build-context/libaether_core.so
bindings/dotnet/tests/run-style-publication-tests.sh "$DOTNET" ../build-context/libaether_core.so

$CMAKE -S native -B ../build-off -DCMAKE_BUILD_TYPE=Release \
  -DAETHERUI_BUILD_TESTS=ON -DAETHERUI_ENABLE_STATE_INCREMENTAL=OFF \
  -DAETHERUI_ENABLE_CONTEXT_OWNERS=OFF -DAETHERUI_ENABLE_WORK_COUNTERS=OFF
$CMAKE --build ../build-off -j4
$CTEST --test-dir ../build-off --output-on-failure -j4
cp -L ../build-off/libaether_core.so artifacts/native/linux-x64/lib/libaether_core.so
bindings/dotnet/tests/run-style-publication-tests.sh "$DOTNET" ../build-off/libaether_core.so

$DOTNET build bindings/dotnet/tests/AetherUI.Tests -c Release -m:1 \
  -p:UseSharedCompilation=false -p:EnableWindowsTargeting=true
OUT=bindings/dotnet/tests/AetherUI.Tests/bin/Release/net10.0-windows10.0.19041.0
cp -L ../build-off/libaether_core.so "$OUT/libaether_core.so"
cmp ../build-off/libaether_core.so "$OUT/libaether_core.so"
sha256sum ../build-off/libaether_core.so "$OUT/libaether_core.so"
set +e
$DOTNET "$OUT/AetherUI.Tests.dll"
managed_status=$?
set -e
test "$managed_status" = 1
# Current inherited Linux result is exit 1, 96/100, not a passing complete suite.
# Four failures: Flex alignment preserves signed residual space;
# Calc retains dimensional types in length and line-height contexts;
# File image loader decodes a file PNG; Composite image loader falls through schemes.

$DOTNET build bindings/dotnet/tools/AetherUI.Validator -c Release -m:1 \
  -p:UseSharedCompilation=false -p:EnableWindowsTargeting=true
# Windows-targeted managed cross-build only, never Windows execution.
```

The managed D2 gate needs the ON diagnostic library and explicitly enables only its internal host test opt-in. It must not be run against OFF and then treated as an ordinary passing smoke test. Full native oracle calls are deliberately outside measured intervals; check raw output to distinguish owner counts from the boolean oracle agreement.
