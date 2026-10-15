#!/bin/bash
# Compares src/gz/Bit1.cpp against the Ghidra decompilation of the original Bit1_* functions.
# Needs work/bit1/oracle.c (generated from scripts/dec_addrs.py output of 0x10014b508..0x10014bc84).
set -e
cd "$(dirname "$0")/.."
clang -std=gnu11 -c work/bit1/oracle.c -o /tmp/bit1_oracle.o -Wno-everything
clang++ -std=c++20 -fsanitize=address,undefined -Isrc scripts/tests/gz_bit1_oracle_driver.cpp src/gz/Bit1.cpp /tmp/bit1_oracle.o -o /tmp/bit1_test
/tmp/bit1_test
