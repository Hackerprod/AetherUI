#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
dotnet=${1:?usage: run-context-owner-tests.sh DOTNET NATIVE_LIBRARY}
native=$(realpath "${2:?native library required; context owners, state incremental and work counters must be ON}")
export DOTNET_CLI_HOME=${DOTNET_CLI_HOME:-/tmp/aether-dotnet-home}
export DOTNET_CLI_TELEMETRY_OPTOUT=1
"$dotnet" build "$repo/bindings/dotnet/tests/AetherUI.StylePublicationTests" -c Release -m:1 -p:UseSharedCompilation=false
output="$repo/bindings/dotnet/tests/AetherUI.StylePublicationTests/bin/Release/net10.0"
cp -L "$native" "$output/libaether_core.so"
cmp "$native" "$output/libaether_core.so"
sha256sum "$native" "$output/libaether_core.so"
"$dotnet" "$output/AetherUI.StylePublicationTests.dll" --d2
