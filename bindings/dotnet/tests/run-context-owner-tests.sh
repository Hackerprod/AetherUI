#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
dotnet=${1:?usage: run-context-owner-tests.sh DOTNET NATIVE_LIBRARY [OUTPUT_ROOT]}
native=$(realpath "${2:?native library required; context owners, state incremental and work counters must be ON}")
export DOTNET_CLI_HOME=${DOTNET_CLI_HOME:-/tmp/aether-dotnet-home}
export DOTNET_CLI_TELEMETRY_OPTOUT=1
out=${3:-"$repo/artifacts/managed-context-owners"}
mkdir -p "$out"
out=$(realpath "$out")
project="$repo/bindings/dotnet/tests/AetherUI.StylePublicationTests/AetherUI.StylePublicationTests.csproj"
properties=(-p:Configuration=Release -p:UseSharedCompilation=false -p:UseArtifactsOutput=true "-p:ArtifactsPath=$out" "-p:AetherNativeLibraryPath=$native")
"$dotnet" build "$project" -m:1 "${properties[@]}"
target=$("$dotnet" msbuild "$project" -nologo -getProperty:TargetPath "${properties[@]}")
[[ -f "$target" ]] || { printf 'Missing test assembly: %s\n' "$target" >&2; exit 1; }
output=$(dirname "$target")
cp -L "$native" "$output/libaether_core.so"
cmp "$native" "$output/libaether_core.so"
sha256sum "$native" "$output/libaether_core.so"
"$dotnet" "$target" --d2
