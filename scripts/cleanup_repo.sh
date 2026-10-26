#!/usr/bin/env bash
# Moves test files and unreferenced C++ sources into _cleanup_archive/ with git mv (reversible with git mv back).
# Usage: scripts/cleanup_repo.sh [--dry-run] [--apply] [--protect <path-prefix>]...
# Without --apply nothing moves; --dry-run is the default and is accepted explicitly.
set -euo pipefail

APPLY=0
PROTECT=("src/gz/" "src/sc4/" "src/rz/" "src/s3d/" "src/render/" "src/app/" "src/jobs/" "scripts/native_" "CMakeLists.txt" "Makefile")
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) APPLY=0 ;;
    --apply) APPLY=1 ;;
    --protect) shift; PROTECT+=("$1") ;;
    -h|--help) sed -n 2,4p "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
ARCHIVE="_cleanup_archive"
TESTS_DIR="$ARCHIVE/tests"
UNUSED_DIR="$ARCHIVE/unused_source"

if [ "$APPLY" = 1 ] && [ -n "$(git status --porcelain)" ]; then
  echo "refusing to move files with a dirty working tree; commit or stash first" >&2
  exit 1
fi

is_protected() {
  local f="$1" p
  for p in "${PROTECT[@]}"; do
    case "$f" in "$p"*) return 0 ;; esac
  done
  return 1
}

# Build-pipeline text: every file that names sources (link lists, scripts, build files).
BUILD_TEXT="$(mktemp)"
trap 'rm -f "$BUILD_TEXT"' EXIT
git ls-files | grep -E '(^|/)(CMakeLists\.txt|Makefile)$|\.(cmake|txt|sh|py|mk)$' | grep -v "^$ARCHIVE/" | xargs cat > "$BUILD_TEXT" 2>/dev/null || true

moved_tests=0; moved_unused=0; kept=0
move_file() {
  local f="$1" dest="$2"
  echo "  $f -> $dest/$f"
  if [ "$APPLY" = 1 ]; then
    mkdir -p "$dest/$(dirname "$f")"
    git mv "$f" "$dest/$f"
  fi
}

echo "== test, mock and suite files =="
while IFS= read -r f; do
  case "$f" in "$ARCHIVE"/*) continue ;; esac
  lower="$(printf '%s' "$f" | tr '[:upper:]' '[:lower:]')"
  case "$lower" in *test*|*mock*|*catch*|*gtest*) ;; *) continue ;; esac
  case "$f" in *.cpp|*.h|*.hpp|*.cc|*.mm|*.c) ;; *) continue ;; esac
  if is_protected "$f" || grep -qF "$(basename "$f")" "$BUILD_TEXT"; then
    echo "  keep (referenced by the build or protected): $f"; kept=$((kept+1)); continue
  fi
  move_file "$f" "$TESTS_DIR"; moved_tests=$((moved_tests+1))
done < <(git ls-files)

echo "== sources with no include and no build reference =="
SOURCES="$(git ls-files '*.cpp' '*.h' '*.hpp' '*.cc' | grep -v "^$ARCHIVE/")"
while IFS= read -r f; do
  [ -z "$f" ] && continue
  if is_protected "$f"; then continue; fi
  base="$(basename "$f")"
  if grep -qF "$base" "$BUILD_TEXT"; then continue; fi
  stem="${base%.*}"
  # included by name anywhere in the other sources?
  if git grep -qE "#[[:space:]]*include[[:space:]]*[<\"][^>\"]*${base//./\\.}[>\"]" -- '*.cpp' '*.h' '*.hpp' '*.cc' '*.mm' ':!'"$f"; then continue; fi
  # a .cpp whose header is still included is part of that header's module
  case "$f" in *.cpp|*.cc)
    if git grep -qE "#[[:space:]]*include[[:space:]]*[<\"][^>\"]*/?${stem}\.(h|hpp)[>\"]" -- '*.cpp' '*.h' '*.hpp' '*.cc' '*.mm' ':!'"$f"; then continue; fi ;;
  esac
  move_file "$f" "$UNUSED_DIR"; moved_unused=$((moved_unused+1))
done <<< "$SOURCES"

echo "== summary =="
echo "tests: $moved_tests  unused: $moved_unused  kept: $kept  mode: $([ "$APPLY" = 1 ] && echo applied || echo dry-run)"
if [ "$APPLY" = 1 ]; then
  echo "verify: rebuild and run scripts/run_tests.sh; undo with 'git mv' or 'git reset --hard HEAD' before committing"
fi
