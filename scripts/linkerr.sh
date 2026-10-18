#!/bin/zsh
# usage: scripts/linkerr.sh <test-name> — like runone.sh but prints the linker's undefined / duplicate symbols
cd "$(dirname "$0")/.."
D=$(grep "^ \"$1:" scripts/run_tests.sh | sed 's/^ "[^:]*://; s/"$//; s/\$T/scripts\/tests/g; s/\$S/src/g')
clang++ -std=c++20 -Wno-overloaded-virtual -Isrc -Iscripts/tests/stubs -fsanitize=address,undefined ${=D} -o ${TMPDIR:-/tmp}/t_$1 2>&1 | grep -E "Undefined|^  \"|duplicate|error" | sort -u | head -${2:-20}
