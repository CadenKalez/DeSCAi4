#!/bin/bash
# Build and run every recovered-code test from scratch (ASan + UBSan). Usage: scripts/run_tests.sh
set -u
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/sc4-tests"; rm -rf "$OUT"; mkdir -p "$OUT"
FLAGS=(-std=c++20 -Wall -Wextra -O1 -fsanitize=address,undefined -Isrc -Iscripts/tests/stubs)
T=scripts/tests; S=src
declare -a TESTS=(
 "layout:$T/audio_snd_layout_test.cpp"
 "gz_packedfile:$T/gz_packedfile_test.cpp $S/gz/cGZPersistDBSegmentMultiPackedFiles.cpp $S/gz/cGZDBSegmentPackedFile.cpp $S/gz/cGZDBRecords.cpp $S/gz/cGZPersistDBSerialRecord.cpp $S/gz/cGZPersistDBSegment.cpp $S/gz/cGZPersistResourceKeyList.cpp $S/rz/cRZFile.cpp $S/rz/cRZString.cpp $S/rz/cRZFastCompression3.cpp $S/dbpf/DBPFReader.cpp"
 "rz_fastcompression:$T/rz_fastcompression_test.cpp $S/rz/cRZFastCompression3.cpp $S/rz/cRZString.cpp $S/dbpf/DBPFReader.cpp"
 "rz_file:$T/rz_file_test.cpp $S/rz/cRZFile.cpp $S/rz/cRZString.cpp"
 "rz_support:$T/rz_support_test.cpp $S/rz/cRZRandom.cpp $S/rz/cRZCriticalSection.cpp $S/rz/cRZCallbackTimer.cpp"
 "streams:$T/rz_stream_test.cpp $S/rz/cRZStream.cpp $S/rz/cRZString.cpp"
 "string:$T/cRZString_test.cpp $S/rz/cRZString.cpp $S/rz/cRZDate.cpp"
 "date:$T/cRZDate_test.cpp $S/rz/cRZDate.cpp"
 "timer:$T/cRZTimer_test.cpp $S/rz/cRZTimer.cpp $S/platform/MacTools.cpp"
 "hit_foundation:$T/audio_hit_test.cpp $S/audio/HitRegisters.cpp $S/audio/HitSem.cpp $S/audio/HitTrackDataBlock.cpp $S/rz/cRZString.cpp"
 "hitlist:$T/audio_hitlist_test.cpp $S/audio/HitList.cpp $S/rz/RZConvert.cpp $S/rz/cRZString.cpp"
 "hitresource:$T/audio_hitresource_test.cpp $S/audio/HitList.cpp $S/rz/RZConvert.cpp $S/rz/cRZString.cpp"
 "freshcell:$T/audio_freshcell_test.cpp $S/audio/FreshCell.cpp $S/rz/cRZString.cpp"
 "sc4_sems:$T/sc4audio_sems_test.cpp $S/sc4audio/EmanationModels.cpp $S/sc4audio/SemManager.cpp $S/sc4audio/SC4AudioHost.cpp $S/audio/HitMan.cpp $S/audio/HitServices.cpp $S/audio/HitControlGroup.cpp $S/audio/HitSem.cpp $S/audio/HitRegisters.cpp $S/audio/HitTrackDataBlock.cpp $S/rz/cRZRandom.cpp $S/rz/cRZString.cpp"
 "sc4_soundmanager:$T/sc4audio_soundmanager_test.cpp $S/sc4audio/SoundManager.cpp $S/sc4audio/SC4AudioHost.cpp $S/sc4/cSC4ResponseCurve.cpp"
 "sc4_listener:$T/sc4audio_listener_test.cpp $S/sc4audio/Listener.cpp $S/sc4audio/EaseSound.cpp $S/sc4audio/SC4AudioHost.cpp $S/rz/cRZMessage2Standard.cpp $S/sc4/SC4GeometryUtilities.cpp"
 "gzsnd:$T/audio_gzsnd_test.cpp $S/audio/GZSnd.cpp $S/audio/GZSnd3D.cpp $S/audio/PCMPlayer.cpp $S/audio/AudioServices.cpp $S/audio/platform/AudioPlatform.cpp $S/audio/SegmentDataSources.cpp $S/sc4audio/SC4AudioHost.cpp $S/rz/cRZString.cpp $S/rz/RZConvert.cpp"
 "sndeventstd:$T/audio_sndeventstd_test.cpp $S/audio/GZSndEventStd.cpp $S/rz/cRZMessage2Standard.cpp"
 "gzsndmgr:$T/audio_gzsndmgr_test.cpp $S/audio/GZSndMgr.cpp $S/audio/AudioServices.cpp $S/rz/cRZString.cpp $S/rz/nRZHash.cpp"
 "gzsndsys:$T/audio_gzsndsys_test.cpp $S/audio/GZSndSys.cpp $S/audio/GZSndSysMac.cpp $S/audio/GZSndCaches.cpp $S/audio/GZSnd3D.cpp $S/audio/GZSnd.cpp $S/audio/PCMPlayer.cpp $S/audio/AudioServices.cpp $S/audio/platform/AudioPlatform.cpp $S/audio/GZSndEventStd.cpp $S/audio/GZSndDefaultEventHandler.cpp $S/gz/cGZMessageQueue2.cpp $S/gz/cGZPropertySet.cpp $S/rz/cRZMessage2Standard.cpp $S/rz/cRZCallbackTimer.cpp $S/rz/cRZTimer.cpp $S/rz/cRZCriticalSection.cpp $S/rz/cRZString.cpp $S/sc4audio/SC4AudioHost.cpp $S/rz/RZConvert.cpp $S/platform/MacTools.cpp"
 "hitdebuginfo:$T/audio_hitdebuginfo_test.cpp $S/audio/HitDebugInfo.cpp $S/rz/cRZPath.cpp $S/rz/cRZString.cpp $S/audio/HitMan.cpp $S/audio/HitServices.cpp $S/audio/HitControlGroup.cpp $S/audio/HitSem.cpp $S/audio/HitRegisters.cpp $S/audio/HitTrackDataBlock.cpp $S/rz/cRZRandom.cpp"
 "hitcreators:$T/audio_hitcreators_test.cpp $S/audio/HitSegmentCreators.cpp $S/audio/GZSnd.cpp $S/audio/PCMPlayer.cpp $S/audio/AudioServices.cpp $S/audio/platform/AudioPlatform.cpp $S/rz/cRZString.cpp $S/audio/HitMan.cpp $S/audio/HitServices.cpp $S/audio/HitControlGroup.cpp $S/audio/HitSem.cpp $S/audio/HitRegisters.cpp $S/audio/HitTrackDataBlock.cpp $S/rz/cRZRandom.cpp $S/gz/cGZPropertySet.cpp"
 "propertyset_io:$T/gz_propertyset_io_test.cpp $S/audio/HitPropertySetResource.cpp $S/gz/cGZPropertySet.cpp $S/gz/cGZPropertySetSerializers.cpp $S/gz/XmlSax.cpp $S/rz/nRZHash.cpp $S/rz/RZConvert.cpp $S/rz/cRZString.cpp $S/rz/cRZStream.cpp"
 "propertyset:$T/gz_propertyset_test.cpp $S/gz/cGZPropertySet.cpp $S/rz/cRZString.cpp"
 "hitchannel:$T/audio_hitchannel_test.cpp $S/audio/HitChannel.cpp $S/audio/HitServices.cpp $S/audio/HitRegisters.cpp $S/gz/cGZPropertySet.cpp $S/rz/cRZString.cpp"
 "hittrackplayer:$T/audio_hittrackplayer_test.cpp $S/audio/HitTrackPlayer.cpp $S/audio/HitChannel.cpp $S/audio/HitMan.cpp $S/audio/HitServices.cpp $S/audio/HitControlGroup.cpp $S/audio/HitSem.cpp $S/audio/HitRegisters.cpp $S/audio/HitTrackDataBlock.cpp $S/gz/cGZPropertySet.cpp $S/rz/cRZRandom.cpp $S/rz/cRZString.cpp"
 "hitman:$T/audio_hitman_test.cpp $S/audio/HitMan.cpp $S/audio/HitServices.cpp $S/audio/HitControlGroup.cpp $S/audio/HitSem.cpp $S/audio/HitRegisters.cpp $S/audio/HitTrackDataBlock.cpp $S/rz/cRZRandom.cpp $S/rz/cRZString.cpp"
 "hitcontrolgroup:$T/audio_hitcontrolgroup_test.cpp $S/audio/HitControlGroup.cpp $S/rz/cRZString.cpp"
 "freshplayer:$T/audio_freshplayer_test.cpp $S/audio/FreshPlayer.cpp $S/audio/FreshDimensionPlayer.cpp $S/audio/FreshCellPlayer.cpp $S/audio/FreshScore.cpp $S/audio/FreshCell.cpp $S/audio/FreshHost.cpp $S/rz/cRZRandom.cpp $S/rz/cRZCriticalSection.cpp $S/rz/cRZCallbackTimer.cpp $S/rz/cRZTimer.cpp $S/platform/MacTools.cpp $S/rz/RZConvert.cpp $S/rz/cRZString.cpp $S/rz/cRZDate.cpp"
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
