#!/bin/bash
# Regenerate the symbol/RTTI dumps in notes/ from the copied main binary.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/game-binaries/MacOS/Sim City 4 Deluxe Editionsub"; N="$ROOT/notes"
nm -U "$B" | sort > "$N/symbols-nm.txt"
nm -U "$B" | awk '$2 ~ /[Tt]/ {print $1, $3}' | xcrun c++filt -_ | sort > "$N/symbols-functions-demangled.txt"
nm -U "$B" | grep ' __ZTV' | awk '{print $1,$3}' | xcrun c++filt -_ | sort > "$N/vtables-demangled.txt"
nm -U "$B" | grep ' __ZTI' | awk '{print $1,$3}' | xcrun c++filt -_ | sort > "$N/typeinfo-demangled.txt"
