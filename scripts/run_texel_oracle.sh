#!/bin/bash
# Compares src/render/TexelOps.cpp against the Ghidra decompilation of nsTexelOp (work/texel/oracle_raw.c,
# produced by scripts/gen_texel_oracle.py).
set -e
cd "$(dirname "$0")/.."
python3 scripts/gen_texel_oracle.py >/dev/null
clang -std=gnu11 -c work/texel/oracle_raw.c -o /tmp/texel_oracle.o -Wno-everything
clang++ -std=c++20 -fsanitize=address,undefined -Isrc scripts/tests/render_texelops_oracle_driver.cpp src/render/TexelOps.cpp /tmp/texel_oracle.o -o /tmp/texel_test
/tmp/texel_test
