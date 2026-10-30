// Where the game's own files are: a folder laid out like the Steam bundle's Contents (GameData/ with the .dat packages, MacOS/
// with the original executable). Looked up in this order:
//   1. $SC4_GAME_CONTENTS (that folder itself)
//   2. game/ in the repository (scripts/fetch_gamedata.sh copies it there; git ignores it, it is never pushed)
//   3. the Steam install, ~/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents
#pragma once
#include <string>

namespace platform {

std::string GameContentsDir();   // no trailing slash
std::string GameDataDir();       // GameContentsDir() + "/GameData/"

}  // namespace platform
