#!/bin/zsh
# Builds and runs the UI-path test: draw engine -> cGZDrawContextRenderToSimGL -> native Metal driver (needs a Metal device).
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/render_uipath
clang++ -std=c++20 -O1 -fobjc-arc -w -Isrc -Iscripts/tests -arch arm64 \
  scripts/tests/render_uipath_test.mm src/gz/DrawContextRenderToHardware.cpp src/gz/DrawContextRenderToSimGL.cpp src/gz/BufferSimGLCache.cpp \
  src/gz/Buffer.cpp src/rz/TileSetup.cpp \
  src/render/gd/MetalGDriver.mm src/render/dx7/dx7_to_metal_bridge.cpp src/render/gd/GDriverBase.cpp src/render/GDIndexOps.cpp src/render/SimGL.cpp src/render/TexelOps.cpp \
  src/render/TextureStageData.cpp src/render/gd/TextureTable.cpp src/render/gd/VideoModes.cpp \
  -framework Foundation -framework Metal -framework QuartzCore \
  -o "$OUT" 2>&1 | grep -E "error|undefined|Undefined|\"" | sort -u | head -30
"$OUT"
