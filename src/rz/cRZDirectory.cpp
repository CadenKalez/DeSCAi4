#include "rz/cRZDirectory.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "gz/FileFind.h"

namespace {
const char* const kCurrentDirName = ".";
const char* const kParentDirName = "..";
const char* const kDefaultFilter = "*";

void ReplaceNameBy(cRZDirectoryEntry& entry, const char* name) { entry.name = cRZString(name); }
}  // namespace

// ---- path helpers ---------------------------------------------------------------------------------------

bool MacDirectoryExists(const char* path) {
    struct stat info;
    return stat(path, &info) != -1 && S_ISDIR(info.st_mode);
}

// Walks the path from the end; each '/' closes one component (copied with a trailing '/'); the last buffer takes what is
// left in front. At most `count` buffers are filled; a component of 255 characters or more fails the whole call.
bool SplitDirectory(const char* path, char (*out)[256], int& count) {
    const int length = static_cast<int>(std::strlen(path));
    if (length < 1) {
        count = 0;
        return false;
    }
    int end = length - 1;
    long index = path[length - 1] == '/' ? length - 2 : length - 1;
    int found = 0;
    if (index >= 0) {
        int offset = static_cast<int>(-index);
        long cursor = index + 1;
        long previous = end;
        long nextEnd = end;
        do {
            end = static_cast<int>(nextEnd);
            if (count <= found + 1) break;
            const char c = path[cursor - 1];
            const long here = index;
            if (cursor == 1 || c == '/') {
                int part;
                if (c == '/') {
                    part = end + offset;
                    std::strncpy(out[found], path + cursor, static_cast<std::size_t>(part));
                } else {
                    part = end;
                    std::strncpy(out[found], path, static_cast<std::size_t>(part));
                    index = 0;
                }
                if (static_cast<unsigned>(part - 1) > 0xfd) {
                    count = 0;
                    return false;
                }
                char* tail = out[found] + part;
                if (out[found][part - 1] != '/') *tail++ = '/';
                *tail = '\0';
                nextEnd = index;
                ++found;
            }
            index = here - 1;
            ++offset;
            end = static_cast<int>(nextEnd);
            const bool more = cursor > 0;
            --cursor;
            if (!(cursor != 0 && more)) break;
        } while (true);
        (void)previous;
    }
    bool ok = true;
    int produced = found;
    if (end > 0 && found < count) {
        ok = false;
        produced = 0;
        if (end < 0xff) {
            std::strncpy(out[found], path, static_cast<std::size_t>(end));
            char* tail = out[found] + end;
            if (out[found][end - 1] != '/') *tail++ = '/';
            *tail = '\0';
            produced = found + 1;
            ok = true;
        }
    }
    count = produced;
    return ok;
}

bool SplitDirectory(const cRZString& path, cRZString* out, int& count) {
    char buffers[32][256];
    int parts = 32;
    if (!SplitDirectory(path.ToChar(), buffers, parts)) {
        count = 0;
        return false;
    }
    if (parts < count) count = parts;
    for (int i = 0; i < count; ++i) out[i] = cRZString(buffers[i]);
    return true;
}

// ---- cRZDirectory ---------------------------------------------------------------------------------------

cRZDirectory::cRZDirectory(const cRZString& path, bool readNow, bool includeParent, bool includeCurrent)
    : path_(path.ToChar()), filter_(kDefaultFilter), includeParent_(includeParent), includeCurrent_(includeCurrent) {
    MakeSurePathEndsWithSeparator();
    if (readNow) ReadDirectoryEntries();
}

void cRZDirectory::MakeSurePathEndsWithSeparator() {
    if (!path_.empty() && path_.back() == '/') return;
    path_.push_back('/');
}

bool cRZDirectory::ChangeDirectory(const cRZString& path, bool readNow) {
    path_ = path.ToChar();
    MakeSurePathEndsWithSeparator();
    if (readNow) ReadDirectoryEntries();
    return true;
}

const cRZDirectoryEntry* cRZDirectory::GetNthEntry(std::uint32_t index) const {
    if (count_ == 0) return nullptr;
    auto it = entries_.begin();
    for (std::uint32_t i = 0; i < count_; ++i, ++it)
        if (index == i) return &*it;
    return nullptr;
}

bool cRZDirectory::DoesDirectoryListContainEntry(const cRZString& name) const {
    for (const cRZDirectoryEntry& entry : entries_)
        if (std::strlen(entry.name.ToChar()) == std::strlen(name.ToChar()) && std::strcmp(entry.name.ToChar(), name.ToChar()) == 0) return true;
    return false;
}

void cRZDirectory::SetNewDirectoryEntryFilter(const cRZString& filter, bool readNow) {
    filter_ = filter.ToChar();
    if (filter_.empty()) filter_ = kDefaultFilter;
    if (readNow) ReadDirectoryEntries();
}

bool cRZDirectory::ReadDirectoryEntries() {
    const cRZString search((path_ + filter_).c_str());
    entries_.clear();
    count_ = 0;
    RZFileFindData* found = RZFileFindFirst(search);
    if (!found) return false;
    cRZDirectoryEntry entry;
    entry.directory = this;
    bool sawParent = false, sawCurrent = false;
    do {
        entry.name = found->name;
        if (!found->isDirectory) {
            entry.kind = kRZDirectoryEntryFile;
            entries_.push_back(entry);
            ++count_;
            continue;
        }
        entry.kind = kRZDirectoryEntryDirectory;
        const std::string name = found->name.ToChar();
        if (!sawCurrent && name == kCurrentDirName) {
            sawCurrent = true;
            if (includeCurrent_) { entries_.push_back(entry); ++count_; }
        } else if (!sawParent && name == kParentDirName) {
            sawParent = true;
            if (includeParent_) { entries_.push_back(entry); ++count_; }
        } else {
            entries_.push_back(entry);
            ++count_;
        }
    } while (RZFileFindNext(found));
    RZFileFindFinish(found);
    // add the pseudo entries by hand when the system did not list them (default filter only)
    if (!sawParent && includeParent_ && filter_ == kDefaultFilter && !DoesDirectoryListContainEntry(cRZString(kParentDirName))) {
        ReplaceNameBy(entry, kParentDirName);
        entry.kind = kRZDirectoryEntryDirectory;
        entries_.push_front(entry);
        ++count_;
    }
    if (!sawCurrent && includeCurrent_ && filter_ == kDefaultFilter && !DoesDirectoryListContainEntry(cRZString(kCurrentDirName))) {
        ReplaceNameBy(entry, kCurrentDirName);
        entry.kind = kRZDirectoryEntryDirectory;
        entries_.push_front(entry);
        ++count_;
    }
    return true;
}

bool cRZDirectory::ReadDirectorySpecificEntriesIntoStringList(std::list<cRZString>& out, std::uint32_t flags) {
    const cRZString search((path_ + filter_).c_str());
    RZFileFindData* found = RZFileFindFirst(search);
    if (!found) return false;
    bool sawParent = false, sawCurrent = false;
    do {
        if (!found->isDirectory) {
            if (flags & 8) out.push_back(found->name);
        } else if (flags & 4) {
            const std::string name = found->name.ToChar();
            if (!sawCurrent && name == kCurrentDirName) {
                sawCurrent = true;
                if (flags & 1) out.push_back(found->name);
            } else if (!sawParent && name == kParentDirName) {
                sawParent = true;
                if (flags & 2) out.push_back(found->name);
            } else {
                out.push_back(found->name);
            }
        }
    } while (RZFileFindNext(found));
    RZFileFindFinish(found);
    if ((flags & 2) && !sawParent && filter_ == kDefaultFilter) out.push_front(cRZString(kParentDirName));
    if ((flags & 1) && !sawCurrent && filter_ == kDefaultFilter) out.push_front(cRZString(kCurrentDirName));
    return true;
}

// Lists the files of `path` (as full paths) and then of every sub-directory; the first level is left out of the result
// when `includeStart` is false and this is the outermost call (a function-local depth counter, as in the original).
bool cRZDirectory::ReadDirectoryFileEntriesIntoStringListRecursive(const cRZString& path, std::list<cRZString>& out, bool includeStart) {
    static int nBeenHereCount = 0;
    std::list<cRZString> files, directories;
    ++nBeenHereCount;
    path_ = path.ToChar();
    MakeSurePathEndsWithSeparator();
    ReadDirectoryEntries();
    if (nBeenHereCount > 1 || includeStart) {
        ReadDirectorySpecificEntriesIntoStringList(files, 8);
        for (const cRZString& file : files) {
            std::string full = path.ToChar();
            if (full.empty() || full.back() != '/') full.push_back('/');
            full += file.ToChar();
            out.push_back(cRZString(full.c_str()));
        }
    }
    const std::string savedFilter = filter_;
    filter_ = kDefaultFilter;
    ReadDirectorySpecificEntriesIntoStringList(directories, 4);
    filter_ = savedFilter;
    for (const cRZString& directory : directories) {
        std::string sub = path.ToChar();            // used as given: the caller's path should already end in '/'
        sub += directory.ToChar();
        sub.push_back('/');
        ReadDirectoryFileEntriesIntoStringListRecursive(cRZString(sub.c_str()), out, includeStart);
    }
    --nBeenHereCount;
    return true;
}

// Creates every missing directory of the path in order; on a failure the ones this call made are removed again.
bool cRZDirectory::CreateDirectoryA(const cRZString& path) {
    std::string full = path.ToChar();
    if (full.empty()) return false;
    if (full.back() != '/') full.push_back('/');
    std::size_t start = 0;
    if (full.size() > 2 && full[0] == '\\' && full[1] == '\\' && full[2] != '\\') {     // \\server\share prefix
        int slashes = 0;
        while (start < full.size()) {
            const char c = full[start++];
            if (c == '\\' && ++slashes > 2) break;
        }
    }
    std::list<std::string> made;
    for (std::size_t i = start; i < full.size(); ++i) {
        if (full[i] != '/') continue;
        const std::string prefix = full.substr(0, i + 1);
        if (!MacDirectoryExists(prefix.c_str())) {
            if (mkdir(prefix.c_str(), 0777) == 0) {
                made.push_back(prefix);
            } else {
                for (const std::string& dir : made) rmdir(dir.c_str());
                return false;
            }
        }
    }
    return true;
}

bool cRZDirectory::DoesAnyEntryExistThatMatchesPattern(const cRZString& pattern) {
    RZFileFindData* found = RZFileFindFirst(pattern);
    if (found) RZFileFindFinish(found);
    return found != nullptr;
}

bool cRZDirectory::RemoveDirectoryA(const cRZString& path, bool) { return rmdir(path.ToChar()) != 0; }

bool cRZDirectory::SplitDirectoryPath(const cRZString& path, cRZString& name, cRZString& directory) {
    char buffers[2][256];
    int count = 2;
    if (!SplitDirectory(path.ToChar(), buffers, count)) return false;
    name = cRZString(buffers[0]);
    directory = cRZString(buffers[1]);
    return true;
}

void cRZDirectory::AlphabetizeEntryList(std::list<cRZString>& list) {
    list.sort([](const cRZString& a, const cRZString& b) { return std::strcmp(a.ToChar(), b.ToChar()) < 0; });
}
