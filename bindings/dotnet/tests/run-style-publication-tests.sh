#!/usr/bin/env bash
set -euo pipefail
# Native must already be installed at artifacts/native/<RID>; pass its exact
# library as the second argument to avoid PreserveNewest retaining stale bytes.
repo=$(cd "$(dirname "$0")/../../.." && pwd)
dotnet=${1:?usage: run-style-publication-tests.sh DOTNET NATIVE_LIBRARY}
native=$(realpath "${2:?native library required}")
for project in AetherUI.StylePublicationTests AetherUI.InputTraceTests; do
    "$dotnet" build "$repo/bindings/dotnet/tests/$project" -c Release -m:1 -p:UseSharedCompilation=false
    output="$repo/bindings/dotnet/tests/$project/bin/Release/net10.0"
    cp -L "$native" "$output/$(basename "$native")"
    # Versioned Linux symlink paths still load under the ABI name.
    case "$(basename "$native")" in libaether_core.so*) cp -L "$native" "$output/libaether_core.so";; esac
    cmp "$native" "$output/$(basename "$native")"
    sha256sum "$native" "$output/$(basename "$native")"
    "$dotnet" "$output/$project.dll"
done
