#!/bin/bash
# Compares src/gz/ColorConversion.cpp against the Ghidra decompilation of cGZGraphicSystem's colour functions.
set -e
cd "$(dirname "$0")/.."
mkdir -p work/color
python3 scripts/export_class.py gz '^cGZGraphicSystem$' >/dev/null
SEL='\(float\)\(~-\(uint\)\((\w+) < 0\.0\) & \(uint\)\1 \|\s*\(uint\)\(\1 \+ 360\.0\) & -\(uint\)\(\1 < 0\.0\)\)=>(\1 < 0.0 ? \1 + 360.0 : \1)'
SEL2='\(float\)\(~-\(uint\)\(360\.0 < (\w+)\) & \(uint\)\1 \|\s*\(uint\)\(\1 \+ -360\.0\) & -\(uint\)\(360\.0 < \1\)\)=>(360.0 < \1 ? \1 + -360.0 : \1)'
python3 scripts/gen_block_oracle.py work/gz/cGZGraphicSystem.txt work/color/oracle.c \
    0x1006184aa 0x1006185c0 0x1006186c6 0x1006187d8 0x100618930 0x100618c6a 0x100618f7e 0x100618f98 0x100618fd8 0x10061faf0 0x10061fbf0 0x10061fcc0 \
    --floatlits 0x1006184aa --floatlits 0x1006185c0 --floatlits 0x1006186c6 --floatlits 0x10061faf0 \
    --sub "$SEL" --sub "$SEL2" --sub '\(uint\)\(param_1 / 60\.0\)=>(uint)(long long)(param_1 / 60.0)'
clang -std=gnu11 -O0 -ffp-contract=off -c work/color/oracle.c -o /tmp/color_oracle.o -Wno-everything
clang++ -std=c++20 -O1 -fsanitize=address,undefined -Isrc scripts/tests/gz_colorconversion_oracle_driver.cpp src/gz/ColorConversion.cpp /tmp/color_oracle.o -o /tmp/color_test
/tmp/color_test
