#!/usr/bin/env bash
# install_library.sh — install the real FidelityLab package into the console
# library (DK0-M3 §24). Used by the CTest fixture and the documented workflow
# (docs/developer/DK0-M3-package-workflow.md). Exits non-zero on any failure
# so the selftest never silently runs without the package.
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

LIBRARY_ROOT="${1:-${LOCALAPPDATA}/11vated/console/library}"
PKG=build/Release/FidelityLab.11g

if [ ! -f "$PKG" ]; then
    echo "error: $PKG missing — run tools/pack/fidelitylab.sh first" >&2
    exit 1
fi

./build/Release/dc-packaged install "$PKG" "$LIBRARY_ROOT" --allow-unsigned
./build/Release/dc-packaged active tech.11vated.fidelitylab "$LIBRARY_ROOT"
echo "library ready: $LIBRARY_ROOT"
