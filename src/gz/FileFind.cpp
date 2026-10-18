#include "gz/FileFind.h"

#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

struct RZFileFindDir {
    cRZString path;     // the directory part of the search path
    DIR* handle = nullptr;
};

RZFileFindData* RZFileFindFirst(const cRZString& search) {
    const std::string full(search.ToChar(), search.Strlen());
    std::string dirPart, patternPart;
    const std::size_t slash = full.rfind('/');
    if (slash == std::string::npos) {
        dirPart = "./";
        patternPart = full;
    } else {
        dirPart = full.substr(0, slash + 1);
        patternPart = full.substr(slash + 1);
    }
    DIR* handle = ::opendir(dirPart.c_str());
    if (!handle) return nullptr;
    auto* data = new RZFileFindData();
    auto* dir = new RZFileFindDir();
    dir->handle = handle;
    dir->path.AssignFromCString(dirPart.c_str());
    data->pattern.AssignFromCString(patternPart.c_str());
    data->dir = dir;
    if (RZFileFindNext(data)) return data;
    // No match: the data is released but the directory handle is left open (as in the original).
    delete data->dir;
    delete data;
    return nullptr;
}

bool RZFileFindNext(RZFileFindData* data) {
    RZFileFindDir* dir = data->dir;
    while (dirent* entry = ::readdir(dir->handle)) {
        if (::fnmatch(data->pattern.ToChar(), entry->d_name, FNM_CASEFOLD) != 0) continue;
        data->name.AssignFromCString(entry->d_name);
        const std::string fullPath = std::string(dir->path.ToChar()) + entry->d_name;
        struct stat info;
        if (::stat(fullPath.c_str(), &info) == 0) {
            data->isDirectory = (info.st_mode & S_IFMT) == S_IFDIR;
            return true;
        }
        return false;
    }
    return false;
}

void RZFileFindFinish(RZFileFindData* data) {
    if (!data) return;
    ::closedir(data->dir->handle);
    delete data->dir;
    delete data;
}

char* Fullpath(char*, const char*, unsigned long) {
    char scratch[0x201];
    if (::getcwd(scratch, sizeof scratch)) {
        const std::size_t length = std::strlen(scratch);
        if (scratch[length - 1] != '/') {
            scratch[length] = '/';
            scratch[length + 1] = '\0';
        }
    }
    return nullptr;
}

bool Fullpath(cRZString&, const cRZString& path) {
    char buffer[0x200];
    return Fullpath(buffer, path.ToChar(), sizeof buffer) != nullptr;
}
