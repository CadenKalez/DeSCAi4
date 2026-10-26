#!/bin/bash
# Offscreen Metal render of the DX7 emulator shader (needs a Metal device). Usage: scripts/run_dx7_metal_test.sh
set -e
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/dx7_metal_test"
clang++ -std=c++17 -fobjc-arc -fsanitize=thread -Isrc scripts/tests/dx7_metal_test.mm src/render/dx7/dx7_to_metal_bridge.cpp -framework Metal -framework Foundation -o "$OUT"
MTL_SHADER_VALIDATION=1 MTL_DEBUG_LAYER=1 "$OUT"
