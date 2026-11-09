#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
dotnet=${1:?usage: run-dom-commit-allocation-tests.sh DOTNET NATIVE_LIBRARY [OUTPUT_ROOT]}
native=$(realpath "${2:?native library required}")
out=${3:-"$repo/artifacts/managed-dom-commit-allocations"}
mkdir -p "$out"
out=$(realpath "$out")
project="$repo/bindings/dotnet/tests/AetherUI.StylePublicationTests/AetherUI.StylePublicationTests.csproj"
properties=(-p:Configuration=Release -p:UseSharedCompilation=false -p:AetherDomCommitAllocationTesting=true -p:UseArtifactsOutput=true "-p:ArtifactsPath=$out")
"$dotnet" build "$project" -m:1 "${properties[@]}"
target=$("$dotnet" msbuild "$project" -nologo -getProperty:TargetPath "${properties[@]}")
[[ -f "$target" ]] || { printf 'Missing test assembly: %s\n' "$target" >&2; exit 1; }
output=$(dirname "$target")
case "$(basename "$native")" in
    libaether_core.so*) name=libaether_core.so;;
    *) name=$(basename "$native");;
esac
cp -L "$native" "$output/$name"
cmp "$native" "$output/$name"
sha256sum "$native" "$output/$name"
DOTNET_TieredCompilation=0 "$dotnet" "$target" --commit-allocations
# Fresh processes preserve the first-link/first-publication boundary for each
# resolver failure/reentry/disposal control. No dummy commit warms those paths.
DOTNET_TieredCompilation=0 "$dotnet" "$target" --host-preparation-bootstrap retry
DOTNET_TieredCompilation=0 "$dotnet" "$target" --host-preparation-bootstrap dispose
