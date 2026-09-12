#!/usr/bin/env bash
# build_fidelitylab_package.sh — stage + pack the real FidelityLab .11g
# (DK0-M3/M4). The package content is the BUILT title executable
# (dc-sample-native-minimal.exe), not a placeholder: what the console
# launches from the installed package is the real D3D12 title.
# DK0-M4: the fidelity contract + compiled candidate artifact ship INSIDE the
# package (§28) — the runtime loads fidelity data from the installed
# generation view, never from the source tree.
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

BUILD=build/Release
SRC=packages/fidelitylab
STAGE="$SRC/content/windows-x64"
# Latest immutable qualification run (M1 evidence architecture): the newest
# run directory's host-capability.json is the canonical host evidence.
HOST_EVIDENCE="$(ls -d evidence/qualification/runs/*/host-capability.json 2>/dev/null | sort | tail -1)"

if [ ! -f "$BUILD/dc-sample-native-minimal.exe" ]; then
    echo "error: $BUILD/dc-sample-native-minimal.exe missing — build the title first" >&2
    exit 1
fi

mkdir -p "$STAGE"
cp "$BUILD/dc-sample-native-minimal.exe" "$STAGE/FidelityLab.exe"

# DK0-M4 §30: compile the fidelity contract against the current REAL host
# evidence before packing. The compiled artifact is packaged content, so the
# acceptance path never consults a source-tree fidelity file.
if [ ! -f "$HOST_EVIDENCE" ]; then
    echo "error: $HOST_EVIDENCE missing — run a qualification first" >&2
    exit 1
fi
./build/Release/dc-fidelity-compile \
    "$SRC/content/manifests/fidelity.json" "$HOST_EVIDENCE" \
    "$SRC/content/manifests/fidelity.candidates.json" 60 1

./build/Release/dc-pack "$SRC" "$BUILD/FidelityLab.11g" "$@"
./build/Release/dc-verify-package "$BUILD/FidelityLab.11g"
echo "package written: $BUILD/FidelityLab.11g"
