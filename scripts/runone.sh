#!/bin/zsh
# usage: scripts/runone.sh <test-name>  — build and run one test registered in scripts/run_tests.sh
cd "$(dirname "$0")/.."
D=$(grep "^ \"$1:" scripts/run_tests.sh | sed 's/^ "[^:]*://; s/"$//; s/\$T/scripts\/tests/g; s/\$S/src/g')
OUT=${TMPDIR:-/tmp}/t_$1
clang++ -std=c++20 -Wall -Wextra -Wno-overloaded-virtual -Isrc -Iscripts/tests/stubs -fsanitize=address,undefined ${=D} -o $OUT 2>&1 | grep -E "error" | head -20
$OUT
