#!/usr/bin/env bash
set -euo pipefail
# Native must already be installed at artifacts/native/<RID>; pass its exact
# library as the second argument to avoid PreserveNewest retaining stale bytes.
repo=$(cd "$(dirname "$0")/../../.." && pwd)
dotnet=${1:?usage: run-style-publication-tests.sh DOTNET NATIVE_LIBRARY [OUTPUT_ROOT]}
native=$(realpath "${2:?native library required}")
out=${3:-"$repo/artifacts/managed-style-publication"}
mkdir -p "$out"
out=$(realpath "$out")
properties=(-p:Configuration=Release -p:UseSharedCompilation=false -p:UseArtifactsOutput=true "-p:ArtifactsPath=$out" "-p:AetherNativeLibraryPath=$native")
for project in AetherUI.StylePublicationTests AetherUI.InputTraceTests; do
    project_file="$repo/bindings/dotnet/tests/$project/$project.csproj"
    "$dotnet" build "$project_file" -m:1 "${properties[@]}"
    target=$("$dotnet" msbuild "$project_file" -nologo -getProperty:TargetPath "${properties[@]}")
    [[ -f "$target" ]] || { printf 'Missing test assembly: %s\n' "$target" >&2; exit 1; }
    output=$(dirname "$target")
    cp -L "$native" "$output/$(basename "$native")"
    # Versioned Linux symlink paths still load under the ABI name.
    case "$(basename "$native")" in libaether_core.so*) cp -L "$native" "$output/libaether_core.so";; esac
    cmp "$native" "$output/$(basename "$native")"
    sha256sum "$native" "$output/$(basename "$native")"
    "$dotnet" "$target"
done
