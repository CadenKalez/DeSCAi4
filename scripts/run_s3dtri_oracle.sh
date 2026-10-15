#!/bin/bash
# Compares src/s3d/TriangleIterator.cpp against the Ghidra decompile of cS3DTriangleIterator's pure functions.
set -e
cd "$(dirname "$0")/.."
mkdir -p work/s3dtri
python3 scripts/export_class.py gz '^cS3DTriangleIterator$' >/dev/null
python3 scripts/gen_block_oracle.py work/gz/cS3DTriangleIterator.txt work/s3dtri/oracle.c 0x1001a9eb8 0x1001aa050
clang -std=gnu11 -O0 -c work/s3dtri/oracle.c -o /tmp/s3dtri_oracle.o -Wno-everything
clang++ -std=c++20 -O1 -fsanitize=address,undefined -Isrc scripts/tests/s3d_triangleiterator_oracle_driver.cpp src/s3d/TriangleIterator.cpp /tmp/s3dtri_oracle.o -o /tmp/s3dtri_test
/tmp/s3dtri_test
