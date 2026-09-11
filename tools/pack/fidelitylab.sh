#!/usr/bin/env bash
# build_fidelitylab_package.sh — stage + pack the real FidelityLab .11g
# (DK0-M3). The package content is the BUILT title executable
# (dc-sample-native-minimal.exe), not a placeholder: what the console
# launches from the installed package is the real D3D12 title.
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

BUILD=build/Release
SRC=packages/fidelitylab
STAGE="$SRC/content/windows-x64"

if [ ! -f "$BUILD/dc-sample-native-minimal.exe" ]; then
    echo "error: $BUILD/dc-sample-native-minimal.exe missing — build the title first" >&2
    exit 1
fi

mkdir -p "$STAGE"
cp "$BUILD/dc-sample-native-minimal.exe" "$STAGE/FidelityLab.exe"

./build/Release/dc-pack "$SRC" "$BUILD/FidelityLab.11g" "$@"
./build/Release/dc-verify-package "$BUILD/FidelityLab.11g"
echo "package written: $BUILD/FidelityLab.11g"
