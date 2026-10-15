#!/bin/bash
# Compares src/s3d/S3DMath.cpp against the Ghidra decompilation of the nS3D helpers and cS3DTransform::TransformPoints.
set -e
cd "$(dirname "$0")/.."
mkdir -p work/s3dmath
python3 scripts/export_class.py gz '^nS3D$' >/dev/null
python3 scripts/export_class.py gz '^cS3DTransform$' >/dev/null
python3 scripts/gen_block_oracle.py work/gz/nS3D.txt work/s3dmath/oracle.c 0x1001c60b8 0x1002e944b 0x1002e9722 0x1002e9802 0x1002e98ef 0x1002e9a63 0x1002e9b45 \
    --sub '\(float\)\(~-\(uint\)\(fVar1 < 0\.0\) & \(uint\)fVar1 \|\s*-\(uint\)\(fVar1 < 0\.0\) & \(uint\)-fVar1\)=>fabsf(fVar1)' \
    --sub '\(float\)\(~-\(uint\)\(fVar3 < 0\.0\) & \(uint\)fVar3 \|\s*\(uint\)-fVar3 & -\(uint\)\(fVar3 < 0\.0\)\)=>fabsf(fVar3)'
python3 scripts/gen_block_oracle.py work/gz/cS3DTransform.txt work/s3dmath/oracle_t.c 0x10017670c
clang -std=gnu11 -O0 -ffp-contract=off -c work/s3dmath/oracle.c -o /tmp/s3d_o1.o -Wno-everything
clang -std=gnu11 -O0 -ffp-contract=off -c work/s3dmath/oracle_t.c -o /tmp/s3d_o2.o -Wno-everything
clang++ -std=c++20 -O1 -fsanitize=address,undefined -Isrc scripts/tests/s3d_math_oracle_driver.cpp src/s3d/S3DMath.cpp src/rz/cRZRandom.cpp /tmp/s3d_o1.o /tmp/s3d_o2.o -o /tmp/s3d_test
/tmp/s3d_test
