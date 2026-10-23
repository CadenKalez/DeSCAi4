#!/bin/bash
# EARLY TEST host build (see src/experimental_host/README_EARLY_TEST.md). Output: build/sc4_host
# Incremental: one object per source under build/host_obj, rebuilt when the source or a header is newer.
set -e
cd "$(dirname "$0")/.."
OBJ=build/host_obj; mkdir -p $OBJ
LIST=${FILES:-$(cat scripts/host_link_list.txt)}
SRCS="$LIST $(ls src/experimental_host/*.cpp src/experimental_host/*.mm)"
CF="-std=c++20 -O1 -g -fobjc-arc -I/opt/homebrew/include/freetype2 -Isrc -Iscripts/tests/stubs -w"
newest_hdr=$(find src -name '*.h' -newer $OBJ/.stamp 2>/dev/null | head -1 || true)
[ -f $OBJ/.stamp ] || newest_hdr=all
OBJS=""
for s in $SRCS; do OBJS="$OBJS $OBJ/$(echo "$s" | tr '/' '_').o"; done
for s in $SRCS; do
  o=$OBJ/$(echo "$s" | tr '/' '_').o
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ -n "$newest_hdr" ]; then echo "$s $o"; fi
done | xargs -P 8 -L 1 sh -c 'clang++ '"$CF"' -c "$0" -o "$1"'
touch $OBJ/.stamp
clang++ $OBJS -L/opt/homebrew/lib -lfreetype -framework Cocoa -framework QuartzCore -framework ImageIO -framework UniformTypeIdentifiers -o build/sc4_host
echo "built build/sc4_host"
