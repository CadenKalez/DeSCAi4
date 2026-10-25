#!/bin/bash
# Builds the native Metal application: build/sc4_game (src/app/game_main.mm + scripts/native_game_link_list.txt, autolinking any
# source an undefined symbol points at). NATIVE_DEBUG=1: -O0 + AddressSanitizer (build/native_obj_debug).
#   build/sc4_game                       open a window
#   build/sc4_game --shot out.png --frames 1200   headless: run N frames, write the frame target
set -e
cd "$(dirname "$0")/.."
NATIVE_OUT=sc4_game python3 scripts/native_autolink.py scripts/native_game_link_list.txt src/app/game_main.mm 14
mkdir -p build
cp "${TMPDIR:-/tmp}/sc4_game${NATIVE_DEBUG:+}" build/sc4_game 2>/dev/null || cp "${TMPDIR:-/tmp}/sc4_game" build/sc4_game
echo "built build/sc4_game"
