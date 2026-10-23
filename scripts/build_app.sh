#!/bin/bash
# Build the native Metal host. Output: build/sc4
#   scripts/build_app.sh            dev build (-O2, as before)
#   scripts/build_app.sh release    -O3, ThinLTO, hidden visibility, dead-code stripping, NDEBUG
#   scripts/build_app.sh debug      -O0 -g with ASan/UBSan
#   ARCH="arm64 x86_64" scripts/build_app.sh release    universal binary (default ARCH=arm64)
set -e
cd "$(dirname "$0")/.."
mkdir -p build
MODE="${1:-dev}"
case "$MODE" in
  dev)     CFLAGS=(-O2); LFLAGS=() ;;
  release) CFLAGS=(-O3 -flto=thin -fvisibility=hidden -fvisibility-inlines-hidden -DNDEBUG); LFLAGS=(-flto=thin -Wl,-dead_strip) ;;
  debug)   CFLAGS=(-O0 -g -fsanitize=address,undefined); LFLAGS=(-fsanitize=address,undefined) ;;
  *) echo "usage: $0 [dev|release|debug]"; exit 2 ;;
esac
ARCH_FLAGS=()
for a in ${ARCH:-arm64}; do ARCH_FLAGS+=(-arch "$a"); done
clang++ -std=c++20 "${CFLAGS[@]}" -fobjc-arc -Isrc "${ARCH_FLAGS[@]}" \
  src/app/main.mm src/sc4/OccupantManager.cpp src/sc4/Occupant.cpp src/sc4/Exemplar.cpp src/sc4/SCProperty.cpp src/sc4/SCPropertyHelp.cpp src/sc4/DBSegment.cpp src/rz/cRZMessage2Standard.cpp src/rz/cRZStream.cpp src/rz/cRZUnknownList.cpp src/rz/CharLength.cpp src/app/GameAudio.mm src/audio/SegmentDataSources.cpp src/audio/AudioServices.cpp src/audio/platform/AudioPlatform.cpp src/gz/cGZPersistResourceManager.cpp src/rz/RZConvert.cpp src/rz/nRZHash.cpp src/render/MetalRenderer.mm src/render/BuildingMeshes.cpp src/game/GameData.cpp src/s3d/S3DModel.cpp src/s3d/FSH.cpp src/sc4/CityBuildings.cpp src/sc4/BuildingOccupant.cpp src/render/Terrain.cpp src/sc4/cSTETerrain.cpp src/gz/cGZDBSegmentPackedFile.cpp src/gz/cGZDBRecords.cpp src/gz/cGZPersistDBSerialRecord.cpp src/gz/cGZPersistDBSegment.cpp src/gz/cGZPersistResourceKeyList.cpp src/rz/cRZFile.cpp src/rz/cRZString.cpp src/rz/cRZVariant.cpp src/rz/cRZFastCompression3.cpp \
  -framework AVFoundation -framework Foundation -framework Cocoa -framework Metal -framework QuartzCore -framework ImageIO -framework UniformTypeIdentifiers \
  "${LFLAGS[@]}" -o build/sc4
echo "built build/sc4 ($MODE, ${ARCH:-arm64})"
