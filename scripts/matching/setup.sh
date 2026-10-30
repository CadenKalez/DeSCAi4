#!/bin/zsh
# One-time: a Python environment with capstone for scripts/matching (in work/, git-ignored).
set -e
cd "$(dirname "$0")/../.."
python3 -m venv work/matching/venv
work/matching/venv/bin/pip install -q capstone
echo "ok: run scripts/matching/match.sh"
