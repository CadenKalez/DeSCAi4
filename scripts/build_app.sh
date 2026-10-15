#!/bin/bash
# Build the native Metal host (arm64). Output: build/sc4
set -e
cd "$(dirname "$0")/.."
mkdir -p build
clang++ -std=c++20 -O2 -fobjc-arc -Isrc -arch arm64 \
  src/app/main.mm src/sc4/OccupantManager.cpp src/sc4/Occupant.cpp src/sc4/Exemplar.cpp src/sc4/SCProperty.cpp src/sc4/SCPropertyHelp.cpp src/sc4/DBSegment.cpp src/rz/cRZMessage2Standard.cpp src/rz/cRZStream.cpp src/rz/cRZUnknownList.cpp src/rz/CharLength.cpp src/app/GameAudio.mm src/audio/SegmentDataSources.cpp src/audio/AudioServices.cpp src/audio/platform/AudioPlatform.cpp src/gz/cGZPersistResourceManager.cpp src/rz/RZConvert.cpp src/rz/nRZHash.cpp src/render/MetalRenderer.mm src/render/BuildingMeshes.cpp src/game/GameData.cpp src/s3d/S3DModel.cpp src/s3d/FSH.cpp src/sc4/CityBuildings.cpp src/sc4/BuildingOccupant.cpp src/render/Terrain.cpp src/sc4/cSTETerrain.cpp src/gz/cGZDBSegmentPackedFile.cpp src/gz/cGZDBRecords.cpp src/gz/cGZPersistDBSerialRecord.cpp src/gz/cGZPersistDBSegment.cpp src/gz/cGZPersistResourceKeyList.cpp src/rz/cRZFile.cpp src/rz/cRZString.cpp src/rz/cRZVariant.cpp src/rz/cRZFastCompression3.cpp \
  -framework AVFoundation -framework Foundation -framework Cocoa -framework Metal -framework QuartzCore -framework ImageIO -framework UniformTypeIdentifiers \
  -o build/sc4
echo built build/sc4
