#!/bin/bash
# Compare the SoftBlit.h template with the decompiled originals (needs the Ghidra backend on :8089).
set -e
cd "$(dirname "$0")/.."
mkdir -p work/blit
python3 scripts/dec_addrs.py --grep 'nGZDrawSoftRender::cBlitter<' > work/blit/all.txt
python3 scripts/gen_blitter_oracle.py
python3 scripts/gen_blitter_harness.py
cc -std=gnu11 -O1 -w -c work/blit/oracle.c -o work/blit/oracle.o
c++ -std=c++20 -O2 -Isrc work/blit/harness.cpp work/blit/oracle.o -o work/blit/harness
work/blit/harness
