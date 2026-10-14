#!/bin/bash
# Build the audio demo (arm64): build/sc4audio
set -e
cd "$(dirname "$0")/.."
mkdir -p build
SRC="src/app/audio_demo.mm src/audio/SegmentDataSources.cpp src/audio/AudioServices.cpp src/audio/platform/AudioPlatform.cpp
 src/gz/cGZPersistResourceManager.cpp src/gz/cGZDBSegmentPackedFile.cpp src/gz/cGZDBRecords.cpp src/gz/cGZPersistDBSerialRecord.cpp src/gz/cGZPersistDBSegment.cpp
 src/gz/cGZPersistResourceKeyList.cpp src/rz/cRZFile.cpp src/rz/cRZString.cpp src/rz/cRZVariant.cpp src/rz/cRZFastCompression3.cpp src/rz/RZConvert.cpp
 src/rz/nRZHash.cpp"
clang++ -std=c++20 -O2 -fobjc-arc -Isrc -arch arm64 $SRC -framework Foundation -framework AVFoundation -o build/sc4audio
echo built build/sc4audio
