#!/bin/zsh
# Byte-matching check: scripts/matching/match.sh [--opt -Os] [--diff NAME] SOURCE...  (see scripts/matching/match.py)
cd "$(dirname "$0")/../.."
[ -x work/matching/venv/bin/python ] || zsh scripts/matching/setup.sh
exec work/matching/venv/bin/python scripts/matching/match.py "$@"
