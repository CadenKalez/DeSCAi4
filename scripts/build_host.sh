#!/bin/bash
# EARLY TEST host build (see src/experimental_host/README_EARLY_TEST.md). Output: build/sc4_host
# Incremental: one object per source under build/host_obj, rebuilt when the source or any header it includes changed
# (clang -MMD dependency files).
set -e
cd "$(dirname "$0")/.."
OBJ=build/host_obj; mkdir -p $OBJ
LIST=${FILES:-$(cat scripts/host_link_list.txt)}
SRCS="$LIST $(ls src/experimental_host/*.cpp src/experimental_host/*.mm)"
CF="-std=c++20 -O1 -g -fobjc-arc -I/opt/homebrew/include/freetype2 -Isrc -Iscripts/tests/stubs -w"
OBJS=""
for s in $SRCS; do OBJS="$OBJS $OBJ/$(echo "$s" | tr '/' '_').o"; done
for s in $SRCS; do
  o=$OBJ/$(echo "$s" | tr '/' '_').o
  d=${o%.o}.d
  stale=0
  if [ ! -f "$o" ] || [ ! -f "$d" ] || [ "$s" -nt "$o" ]; then stale=1
  elif [ -n "$(sed 's/\\$//' "$d" | tr ' ' '\n' | grep -E '\.(h|inl)$' | while read -r h; do [ -f "$h" ] && [ "$h" -nt "$o" ] && echo "$h"; done | head -1)" ]; then stale=1
  fi
  [ $stale = 1 ] && echo "$s $o"
done | xargs -P 8 -L 1 sh -c 'clang++ '"$CF"' -MMD -c "$0" -o "$1"'
clang++ $OBJS -L/opt/homebrew/lib -lfreetype -framework Cocoa -framework QuartzCore -framework ImageIO -framework UniformTypeIdentifiers -o build/sc4_host
echo "built build/sc4_host"
