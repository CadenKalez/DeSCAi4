// See GameDataPath.h.
#include "platform/GameDataPath.h"

#include <cstdlib>
#include <filesystem>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace platform {
namespace {

namespace fs = std::filesystem;

bool HasGameData(const fs::path& contents) {
    std::error_code error;
    return fs::is_directory(contents / "GameData", error);
}

// game/ next to the working directory or the executable, or in one of their parents (build/ sits inside the repository).
std::string FindRepositoryCopy() {
    std::error_code error;
    std::vector<fs::path> starts{fs::current_path(error)};
#if defined(__APPLE__)
    char buffer[4096];
    std::uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) starts.push_back(fs::path(buffer).parent_path());
#endif
    for (fs::path start : starts) {
        for (fs::path dir = start; !dir.empty(); dir = dir.parent_path()) {
            if (HasGameData(dir / "game")) return (dir / "game").string();
            if (dir == dir.parent_path()) break;
        }
    }
    return {};
}

}  // namespace

std::string GameContentsDir() {
    static const std::string found = [] {
        if (const char* env = std::getenv("SC4_GAME_CONTENTS"); env && *env) return std::string(env);
        if (std::string local = FindRepositoryCopy(); !local.empty()) return local;
        const char* home = std::getenv("HOME");
        return std::string(home ? home : "") +
               "/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents";
    }();
    return found;
}

std::string GameDataDir() { return GameContentsDir() + "/GameData/"; }

}  // namespace platform
