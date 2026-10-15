#!/bin/bash
# Compares src/render/SimGL.cpp against the Ghidra decompilation of nRZSimGL (non-DXT-decoder part).
set -e
cd "$(dirname "$0")/.."
mkdir -p work/simgl
python3 scripts/gen_block_oracle.py work/gz/nRZSimGL.txt work/simgl/oracle.c $(cat scripts/simgl_oracle_addrs.txt) \
    --table kVFOffset:0x100a1a9a0:48 --table kVFMask:0x100a1a9d0:48 --table kVFEltSize:0x100a1aa00:48 \
    --table DAT_100a1aa2c:0x100a1aa2c:40 \
    --sub 'PackStandardVertexFormat\(\)=>oracle_1001e3694(param_1)' \
    --sub '\bStandardVertexFormatStride\s*\(=>oracle_1001e37ee(' --sub '\bStandardVFElementOffset\s*\(=>oracle_1001e3918(' \
    --sub '\bConvertColorKeyToAlphaStridedTwin16\s*\(=>oracle_10035feb2('
clang -std=gnu11 -c work/simgl/oracle.c -o /tmp/simgl_oracle.o -Wno-everything
clang++ -std=c++20 -fsanitize=address,undefined -Isrc scripts/tests/render_simgl_oracle_driver.cpp src/render/SimGL.cpp /tmp/simgl_oracle.o -o /tmp/simgl_test
/tmp/simgl_test
