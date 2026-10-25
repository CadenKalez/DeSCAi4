#!/bin/bash
# Builds build/city_probe (src/tools/city_probe*.cpp, a diagnostic tool): boots the recovered app against the retail game data
# and loads a real city through cSC4App::LoadCity. Incremental (one object per source under build/probe_obj).
#   scripts/build_city_probe.sh [asan]     -> build/city_probe  (asan: -O0 + AddressSanitizer, build/city_probe_asan)
#   build/city_probe [city.sc4]            -> prints OK / FAIL per step; "classes ... could not be created" lists what is missing
set -e
cd "$(dirname "$0")/.."
MODE=${1:-fast}
if [ "$MODE" = asan ]; then OBJ=build/probe_obj_asan; OUT=build/city_probe_asan; OPT="-O0 -g -fno-omit-frame-pointer -fsanitize=address"; LD="-fsanitize=address"
else OBJ=build/probe_obj; OUT=build/city_probe; OPT="-O1 -g"; LD=""; fi
mkdir -p $OBJ
CF="-std=c++20 $OPT -Wno-overloaded-virtual -w -Isrc -Iscripts/tests/stubs"
OBJS=""
for s in $(cat scripts/city_probe_link_list.txt); do OBJS="$OBJS $OBJ/$(echo "$s" | tr '/' '_').o"; done
for s in $(cat scripts/city_probe_link_list.txt); do
  o=$OBJ/$(echo "$s" | tr '/' '_').o
  if [ -f "$o" ] && [ "$o" -nt "$s" ] && [ -z "$(find src -name '*.h' -newer "$o" | head -1)" ]; then continue; fi
  echo "$s $o"
done | xargs -P 8 -L 1 sh -c 'clang++ '"$CF"' -c "$0" -o "$1"'
clang++ $LD $OBJS -o $OUT
echo "built $OUT"
