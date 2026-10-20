#!/bin/bash
# Build and run registered tests (entries of scripts/run_tests.sh) on Linux with clang++: scripts/run_registered_linux.sh name [name...]
cd "$(dirname "$0")/.."
OUT=/tmp/sc4-linux-tests; mkdir -p "$OUT"
for name in "$@"; do
  entry=$(grep -m1 "^ \"$name:" scripts/run_tests.sh | sed 's/^ *"//; s/"$//')
  [ -z "$entry" ] && { echo "NOENTRY $name"; continue; }
  files=$(echo "${entry#*:}" | sed 's/\$S/src/g; s/\$T/scripts\/tests/g')
  if clang++ -std=c++20 -O1 -Dst_atimespec=st_atim -Dst_mtimespec=st_mtim -Dst_ctimespec=st_ctim -D_CS_DARWIN_USER_TEMP_DIR=_CS_PATH -Isrc -Iscripts/tests/stubs $files -lpthread -o "$OUT/$name" 2> "$OUT/$name.log" && "$OUT/$name" > "$OUT/$name.out" 2>&1; then
    echo "PASS $name: $(tail -1 "$OUT/$name.out")"
  else
    echo "FAIL $name"; head -5 "$OUT/$name.log" "$OUT/$name.out"
  fi
done
