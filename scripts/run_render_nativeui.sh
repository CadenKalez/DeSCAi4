#!/bin/zsh
# Window tree on the native Metal driver, headless (needs a Metal device): builds scripts/native_app_link_list.txt (autolinking any
# missing source) with the test as main, then runs it. NATIVE_DEBUG=1 builds -O0 with AddressSanitizer.
cd "$(dirname "$0")/.."
python3 scripts/native_autolink.py scripts/native_app_link_list.txt scripts/tests/render_nativeui_test.mm || exit 1
if [ -n "$NATIVE_DEBUG" ]; then ASAN_OPTIONS=detect_leaks=0 ${TMPDIR:-/tmp}/native_link_probe_debug; else ${TMPDIR:-/tmp}/native_link_probe; fi
