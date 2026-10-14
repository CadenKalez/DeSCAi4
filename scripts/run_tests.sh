#!/bin/bash
# Build and run every recovered-code test from scratch (ASan + UBSan). Usage: scripts/run_tests.sh
set -u
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/sc4-tests"; rm -rf "$OUT"; mkdir -p "$OUT"
FLAGS=(-std=c++20 -Wall -Wextra -O1 -fsanitize=address,undefined -Isrc -Iscripts/tests/stubs)
T=scripts/tests; S=src
declare -a TESTS=(
 "layout:$T/audio_snd_layout_test.cpp"
 "rz_support:$T/rz_support_test.cpp $S/rz/cRZRandom.cpp $S/rz/cRZCriticalSection.cpp $S/rz/cRZCallbackTimer.cpp"
 "streams:$T/rz_stream_test.cpp $S/rz/cRZStream.cpp $S/rz/cRZString.cpp"
 "string:$T/cRZString_test.cpp $S/rz/cRZString.cpp $S/rz/cRZDate.cpp"
 "date:$T/cRZDate_test.cpp $S/rz/cRZDate.cpp"
 "timer:$T/cRZTimer_test.cpp $S/rz/cRZTimer.cpp $S/platform/MacTools.cpp"
 "hit_foundation:$T/audio_hit_test.cpp $S/audio/HitRegisters.cpp $S/audio/HitSem.cpp $S/audio/HitTrackDataBlock.cpp $S/rz/cRZString.cpp"
 "hitlist:$T/audio_hitlist_test.cpp $S/audio/HitList.cpp $S/rz/RZConvert.cpp $S/rz/cRZString.cpp"
 "hitresource:$T/audio_hitresource_test.cpp $S/audio/HitList.cpp $S/rz/RZConvert.cpp $S/rz/cRZString.cpp"
 "freshcell:$T/audio_freshcell_test.cpp $S/audio/FreshCell.cpp $S/rz/cRZString.cpp"
 "freshplayer:$T/audio_freshplayer_test.cpp $S/audio/FreshPlayer.cpp $S/audio/FreshCellPlayer.cpp $S/audio/FreshScore.cpp $S/audio/FreshCell.cpp $S/audio/FreshHost.cpp $S/rz/cRZRandom.cpp $S/rz/cRZCriticalSection.cpp $S/rz/cRZCallbackTimer.cpp $S/rz/cRZTimer.cpp $S/platform/MacTools.cpp $S/rz/RZConvert.cpp $S/rz/cRZString.cpp $S/rz/cRZDate.cpp"
)
fail=0
for entry in "${TESTS[@]}"; do
  name="${entry%%:*}"; files="${entry#*:}"
  # shellcheck disable=SC2086
  if clang++ "${FLAGS[@]}" $files -o "$OUT/$name" 2> "$OUT/$name.log" && "$OUT/$name" > "$OUT/$name.out" 2>&1; then
    printf "PASS  %-16s %s\n" "$name" "$(tail -1 "$OUT/$name.out")"
  else
    printf "FAIL  %s\n" "$name"; head -15 "$OUT/$name.log" "$OUT/$name.out" 2>/dev/null; fail=1
  fi
done
exit $fail
