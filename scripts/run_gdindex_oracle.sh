#!/bin/bash
# Compares src/render/GDIndexOps.cpp against the Ghidra decompilation of the nSGLDX7 helpers.
set -e
cd "$(dirname "$0")/.."
mkdir -p work/gdindex
python3 scripts/gen_block_oracle.py work/gz/nSGLDX7.txt work/gdindex/oracle.c $(cat scripts/gdindex_oracle_addrs.txt) \
    --table DAT_100a26980:0x100a26980:24 --table DAT_100a269a0:0x100a269a0:32 --table DAT_100a26f40:0x100a26f40:60
clang -std=gnu11 -c work/gdindex/oracle.c -o /tmp/gdindex_oracle.o -Wno-everything
clang++ -std=c++20 -fsanitize=address,undefined -Isrc scripts/tests/render_gdindexops_oracle_driver.cpp src/render/GDIndexOps.cpp /tmp/gdindex_oracle.o -o /tmp/gdindex_test
/tmp/gdindex_test
