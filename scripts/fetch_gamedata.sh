#!/bin/zsh
# Copies the game's own files from your SimCity 4 Deluxe install into game/ in this repository, where the build looks first
# (src/platform/GameDataPath.h). game/ is git-ignored and the pre-commit hook refuses it: the game data is copyrighted and is
# never pushed to GitHub. Usage:
#   scripts/fetch_gamedata.sh                       # from the default Steam install
#   scripts/fetch_gamedata.sh "/path/to/Sim City 4 Deluxe Edition.app/Contents"
set -eu
cd "$(dirname "$0")/.."
SRC="${1:-$HOME/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents}"
if [[ ! -d "$SRC/GameData" ]]; then
  echo "No GameData folder in: $SRC"
  echo "Install SimCity 4 Deluxe Edition (Steam, macOS) or pass the bundle's Contents folder."
  exit 1
fi
mkdir -p game
echo "Copying GameData ($(du -sh "$SRC/GameData" | cut -f1)) and MacOS into game/ ..."
rsync -a --delete "$SRC/GameData/" game/GameData/
[[ -d "$SRC/MacOS" ]] && rsync -a --delete "$SRC/MacOS/" game/MacOS/
git check-ignore -q game/GameData || { echo "WARNING: game/ is not git-ignored"; exit 1; }
echo "Done: game/ ($(du -sh game | cut -f1)). It is ignored by git and never pushed."
