#!/bin/zsh
# usage: scripts/run_machine_oracle.sh <name>
# Builds scripts/machine_oracle/<name>_oracle.cpp for x86_64 together with the sources of the matching unit test
# (scripts/run_tests.sh entry sc4_<name>) and runs it under Rosetta against the retail machine code (GameImage.h).
cd "$(dirname "$0")/.."
D=$(grep "^ \"sc4_$1:" scripts/run_tests.sh | sed 's/^ "[^:]*://; s/"$//; s/\$T/scripts\/tests/g; s/\$S/src/g' |
    tr ' ' '\n' | grep -v '_test\.cpp$' | tr '\n' ' ')
OUT=${TMPDIR:-/tmp}/machine_oracle_$1
clang++ -arch x86_64 -std=c++20 -O1 -ffp-contract=off -w -Isrc -Iscripts/tests/stubs -Iscripts/machine_oracle \
    scripts/machine_oracle/$1_oracle.cpp ${=D} -o $OUT || exit 1
shift
arch -x86_64 $OUT "$@"   # extra arguments go to the oracle
