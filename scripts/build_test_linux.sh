#!/bin/bash
# Build and run one recovered-code test on Linux with clang++ (the macOS-only bits of cRZFile are mapped to their glibc names).
# usage: scripts/build_test_linux.sh scripts/tests/<name>_test.cpp [extra source .cpp ...]
# A common set of framework sources (property sets, DB segments, streams, strings, grids, response curves, random) is always
# linked; pass the class's own .cpp files and anything else the test needs as extra sources. Output: /tmp/sc4-linux-tests/<name>.
set -e
cd "$(dirname "$0")/.."
T="$1"; shift
OUT=/tmp/sc4-linux-tests; mkdir -p "$OUT"
NAME=$(basename "$T" .cpp)
COMMON="src/sc4/Budget.cpp src/sc4/DBSegment.cpp src/gz/StreamOps.cpp src/sc4/SCProperty.cpp src/sc4/SCPropertyHelp.cpp src/sc4/Exemplar.cpp src/rz/cRZVariant.cpp src/rz/cRZString.cpp src/rz/nRZHash.cpp src/rz/RZConvert.cpp src/rz/CharLength.cpp src/rz/cRZStream.cpp src/rz/cRZMessage2Standard.cpp src/rz/cRZUnknownList.cpp src/gz/cGZPersistResourceKeyList.cpp src/gz/cGZPersistResourceManager.cpp src/gz/cGZPersistDBSegment.cpp src/gz/cGZPersistDBSerialRecord.cpp src/gz/cGZDBRecords.cpp src/gz/cGZDBSegmentPackedFile.cpp src/rz/cRZFile.cpp src/rz/cRZFastCompression3.cpp src/gz/cGZDate.cpp src/rz/cRZDate.cpp src/rz/cRZTimer.cpp src/platform/MacTools.cpp src/sc4/SimStreamOps.cpp src/sc4/RandomIterator.cpp src/sc4/cSC4SimGrid.cpp src/sc4/cSC4ResponseCurve.cpp src/rz/cRZRandom.cpp"
# drop duplicates of the extras
EXTRA="$*"
SRCS=""
for f in $COMMON; do case " $EXTRA " in *" $f "*) ;; *) case " $SRCS " in *" $f "*) ;; *) SRCS="$SRCS $f";; esac;; esac; done
clang++ -std=c++20 -Wall -Wextra -O1 -Dst_atimespec=st_atim -Dst_mtimespec=st_mtim -Dst_ctimespec=st_ctim -D_CS_DARWIN_USER_TEMP_DIR=_CS_PATH \
  -Isrc -Iscripts/tests/stubs "$T" $EXTRA $SRCS -lpthread -o "$OUT/$NAME"
"$OUT/$NAME"
