// cRZDirectory — a directory listing with a wildcard filter (21 functions, 0x100031d40..0x10003436e) plus the two path
// helpers SplitDirectory (0x100034a6b / 0x100034c11) and MacDirectoryExists (0x1007fa00f).
// Layout: +0 entry list (count at +0x18), +0x28 directory path (always ends in '/'), +0x50 filter (default "*"),
// +0x70 include "..", +0x71 include ".". The listing is read through RZFileFindFirst/Next. Names of directories and
// plain files go into the list with kind 4 and 8; the pseudo entries "." and ".." are listed only when asked for, and
// are added in front by hand when the platform did not report them and the filter is the default one.
// String-list reads take flags: 1 ".", 2 "..", 4 directories, 8 files.
// Quirks kept: SplitDirectoryPath hands back the last component and the parent in the order (name, directory);
// RemoveDirectoryA returns true when rmdir fails; Alphabetize sorts the list with operator<.
#pragma once
#include <cstdint>
#include <list>
#include <string>

#include "rz/cRZString.h"

constexpr std::uint32_t kRZDirectoryEntryDirectory = 4, kRZDirectoryEntryFile = 8;

class cRZDirectory;
struct cRZDirectoryEntry {
    cRZDirectory* directory = nullptr;      // +0
    cRZString name;                         // +0x10
    std::uint32_t kind = 0;                 // +0x38 (4 directory, 8 file)
};

// Splits "/a/b/c" into "c/" then "/a/b/" (up to `count` buffers of 256 bytes); false when a part does not fit.
bool SplitDirectory(const char* path, char (*out)[256], int& count);
bool SplitDirectory(const cRZString& path, cRZString* out, int& count);
bool MacDirectoryExists(const char* path);

class cRZDirectory {
public:
    cRZDirectory(const cRZString& path, bool readNow, bool includeParent, bool includeCurrent);
    ~cRZDirectory() = default;

    std::uint32_t GetDirectoryEntryCount() const { return count_; }
    const cRZDirectoryEntry* GetNthEntry(std::uint32_t index) const;
    bool ReadDirectoryEntries();
    bool ChangeDirectory(const cRZString& path, bool readNow);
    void SetNewDirectoryEntryFilter(const cRZString& filter, bool readNow);
    bool DoesDirectoryListContainEntry(const cRZString& name) const;
    bool ReadDirectorySpecificEntriesIntoStringList(std::list<cRZString>& out, std::uint32_t flags);
    bool ReadDirectoryFileEntriesIntoStringList(std::list<cRZString>& out) { return ReadDirectorySpecificEntriesIntoStringList(out, 8); }
    bool ReadDirectoryDirectoryEntriesIntoStringList(std::list<cRZString>& out, std::uint32_t flags) {
        return ReadDirectorySpecificEntriesIntoStringList(out, (flags & 3) | 4);
    }
    bool ReadDirectoryFileEntriesIntoStringListRecursive(const cRZString& path, std::list<cRZString>& out, bool includeStart);

    static bool DoesDirectoryExist(const cRZString& path) { return MacDirectoryExists(path.ToChar()); }
    static bool CreateDirectoryA(const cRZString& path);
    static bool DoesAnyEntryExistThatMatchesPattern(const cRZString& pattern);
    static bool RemoveDirectoryA(const cRZString& path, bool recursive = false);
    static bool SplitDirectoryPath(const cRZString& path, cRZString& name, cRZString& directory);
    static void AlphabetizeEntryList(std::list<cRZString>& list);

    const std::string& Path() const { return path_; }
    const std::string& Filter() const { return filter_; }

private:
    void MakeSurePathEndsWithSeparator();

    std::list<cRZDirectoryEntry> entries_;      // +0
    std::uint32_t count_ = 0;                   // +0x18
    std::string path_;                          // +0x28
    std::string filter_;                        // +0x50
    bool includeParent_ = false;                // +0x70
    bool includeCurrent_ = false;               // +0x71
};
