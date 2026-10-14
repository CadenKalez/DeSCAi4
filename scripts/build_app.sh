#!/bin/bash
# Build the native Metal host (arm64). Output: build/sc4
set -e
cd "$(dirname "$0")/.."
mkdir -p build
clang++ -std=c++20 -O2 -fobjc-arc -Isrc -arch arm64 \
  src/app/main.mm src/render/MetalRenderer.mm src/render/Terrain.cpp src/dbpf/DBPFReader.cpp \
  -framework Cocoa -framework Metal -framework QuartzCore -framework ImageIO -framework UniformTypeIdentifiers \
  -o build/sc4
echo built build/sc4
