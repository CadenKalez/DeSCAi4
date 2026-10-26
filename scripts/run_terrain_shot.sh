#!/bin/zsh
# Builds and runs terrain_shot (needs a Metal device): renders a procedural terrain via cSTETerrainView3D -> work/shots/terrain_shot.png
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/terrain_shot
clang++ -std=c++20 -O1 -fobjc-arc -Wall -Wno-overloaded-virtual -Isrc -arch arm64 \
  scripts/terrain_shot.mm src/sc4/cSTETerrainView3D.cpp src/sc4/cSTETerrainView3DDraw.cpp src/sc4/cSTETerrain.cpp src/sc4/GeometryUtilities.cpp \
  src/sc4/PackedRenderState.cpp src/render/gd/MetalGDriver.mm src/render/dx7/dx7_to_metal_bridge.cpp src/render/gd/GDriverBase.cpp src/render/GDIndexOps.cpp src/render/TexelOps.cpp \
  src/render/TextureStageData.cpp src/render/gd/TextureTable.cpp src/render/gd/VideoModes.cpp src/render/SimGL.cpp \
  src/gz/cGZDBSegmentPackedFile.cpp src/gz/cGZDBRecords.cpp src/gz/cGZPersistDBSerialRecord.cpp src/gz/cGZPersistDBSegment.cpp \
  src/gz/cGZPersistResourceKeyList.cpp src/rz/cRZFile.cpp src/rz/cRZString.cpp src/rz/cRZFastCompression3.cpp src/rz/cRZVariant.cpp \
  -framework Foundation -framework Metal -framework QuartzCore -framework ImageIO -framework UniformTypeIdentifiers -framework CoreGraphics \
  -o "$OUT" 2>&1 | grep -E "error|undefined|Undefined" | head -20
"$OUT"
