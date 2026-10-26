#!/bin/zsh
# Builds and runs the native-vs-reference render diff harness (needs a Metal device). PNGs land in work/shots/.
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/render_gd_diff
clang++ -std=c++20 -O1 -fobjc-arc -Wall -Wno-overloaded-virtual -Isrc -Iscripts/tests -arch arm64 \
  scripts/tests/render_gd_diff_test.mm src/render/gd/MetalGDriver.mm src/render/dx7/dx7_to_metal_bridge.cpp src/render/gd/GDriverBase.cpp src/render/MetalGL.mm \
  src/render/GDIndexOps.cpp src/render/SimGL.cpp src/render/TexelOps.cpp src/render/TextureStageData.cpp src/render/gd/TextureTable.cpp src/render/gd/VideoModes.cpp \
  -framework Foundation -framework Metal -framework QuartzCore -framework ImageIO -framework UniformTypeIdentifiers -framework CoreGraphics \
  -o "$OUT" 2>&1 | grep -E "error|undefined|Undefined" | head -20
"$OUT"
