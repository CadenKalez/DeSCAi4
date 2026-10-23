// NetworkFileCache — see NetworkFileCache.h for the object layout, the persisted INI format and the quirks kept.
//
// Every file operation goes through the recovered cRZFile / cRZDirectory / cRZIniFile classes (the file-system seams);
// this translation unit never calls open/stat/readdir itself. Original addresses are in the comments of the header;
// the ones worth repeating next to a call are the cRZFile helpers it uses (GetTempDirectory 0x1000379ae,
// FileExists 0x100037148, Remove 0x1000377c2, GetPathName 0x1000387de, GetPathDirectory 0x100038466,
// Copy 0x10003720e, cRZIniFile::EnumEntries 0x10003013a, WriteEntry 0x10002ec30).
#include "sc4/NetworkFileCache.h"

#include <cstdlib>
#include <cstring>
#include <iterator>
#include <list>
#include <utility>

#include "rz/cRZDirectory.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"

namespace sc4 {
namespace {

// The constants the binary reads through the pointers at 0x100b12f28/30/38/40 (strings at 0x1009f9cf4/1f/35/d11/d2d/d4f/d5e).
constexpr const char* kDefaultIniFileName = "FileCache.ini";
constexpr const char* kDefaultIniFileSection = "Cache Entries";
constexpr const char* kSearchCachedFileExtension = "*.cache";
constexpr const char* kCacheFileExtension = ".cache";
constexpr const char* kCacheDirectoryName = "Maxis Network Cache/";
constexpr const char* kCacheEntryKeyFormat = "CacheEntry%05u";
constexpr const char* kCacheEntryValueFormat = "%s,%s,%u,%u,%u,%d,%d,%d";
constexpr const char* kCacheFileNameFormat = "%d%s";

constexpr std::uint32_t kDefaultFileCacheSize = 0x2dc6c0;    // 3,000,000 bytes
constexpr std::uint32_t kDefaultRAMCacheSize = 0x7a120;      // 500,000 bytes
constexpr std::uint32_t kDefaultExpirationTime = 0x69780;    // 432,000 s = 5 days
constexpr std::uint32_t kMaintenanceInterval = 0x28;         // maintenance runs when the counter passes 40
constexpr std::uint32_t kMaxCacheEntryLength = 0x3d0900;     // 4,000,000: longer INI entries lose their file
constexpr std::uint32_t kNeverExpires = 0x7fffffff;
constexpr std::uint32_t kNewCacheNameAttempts = 10000;       // 0x270f failed opens + 1

// std::operator+(std::string, std::string) wrapped in a cRZString: "<directory><name>".
cRZString MakeCachePath(const cRZString& directory, const cRZString& name) {
    cRZString path;
    path.str() = directory.str() + name.str();
    return path;
}

// The ".cache" suffix the candidate name gets: the key's extension (2..8 characters after the last '.') with every
// character outside [0-9A-Za-z.] erased, then ".cache" appended. Keys without a usable extension get ".cache" only.
std::string CacheFileExtension(const cRZString& key) {
    const std::string& text = key.str();
    std::string extension;
    const std::size_t dot = text.rfind('.');
    if (dot != std::string::npos) {
        const std::uint32_t length = static_cast<std::uint32_t>(text.size());
        const std::uint32_t dotPosition = static_cast<std::uint32_t>(dot);
        if (dotPosition <= length) {
            const std::uint32_t tail = length - dotPosition;
            if (tail != 0 && tail >= 2 && tail - 2 < 7) {
                extension.assign(text, dotPosition, tail);
            }
        }
    }
    std::size_t index = 0;
    while (index < extension.size()) {
        const unsigned char c = static_cast<unsigned char>(extension[index]);
        const bool keep = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.';
        if (keep) {
            ++index;
        } else {
            extension.erase(index, 1);
        }
    }
    extension.append(kCacheFileExtension);
    return extension;
}

std::uint32_t TimeNow32() { return static_cast<std::uint32_t>(std::time(nullptr)); }

}  // namespace

NetworkFileCache::NetworkFileCache() {
    // 0x100630d70: empty directory, "FileCache.ini", the three defaults and an empty map (libc++ default).
    mCacheIniFileName = cRZString(kDefaultIniFileName);
    mFileCacheSize = kDefaultFileCacheSize;
    mRAMCacheSize = kDefaultRAMCacheSize;
    mDefaultExpirationTime = kDefaultExpirationTime;   // the ctor stores 0x69780 in the same qword as the access count
    mAccessCount = 0;
    mMaintenanceCount = 0;
}

NetworkFileCache::~NetworkFileCache() {
    // 0x100630ec6: Shutdown, then the map and the two strings are destroyed (implicitly here).
    Shutdown();
}

bool NetworkFileCache::Init() {
    if (mInitialized) return true;
    if (mCacheDirectory.str().empty()) {
        cRZFile::GetTempDirectory(mCacheDirectory);
        mCacheDirectory.str().append(kCacheDirectoryName);
    }
    if (!cRZDirectory::DoesDirectoryExist(mCacheDirectory)) {
        cRZDirectory::CreateDirectoryA(mCacheDirectory);
    }
    ReadCacheIniFile();
    RemoveUnusedCachedFiles();
    mInitialized = true;
    return true;
}

// 0x100631004: "<cache dir><ini name>" in the "Cache Entries" section. Returns EnumEntries' result (false when the
// file does not exist or has no such section).
bool NetworkFileCache::ReadCacheIniFile() {
    const cRZString path = MakeCachePath(mCacheDirectory, mCacheIniFileName);
    cRZIniFile ini(path);
    const cRZString section(kDefaultIniFileSection);
    return ini.EnumEntries(section, &NetworkFileCache::IniFileCallbackFunction, this);
}

// 0x1006311ba: every "*.cache" file in the cache directory whose name is not one of the cache keys is removed.
// Quirk: the map is keyed by URL, so this deletes all real cache files at start-up.
bool NetworkFileCache::RemoveUnusedCachedFiles() {
    cRZDirectory directory(mCacheDirectory, false, false, false);
    const cRZString filter(kSearchCachedFileExtension);
    directory.SetNewDirectoryEntryFilter(filter, false);
    std::list<cRZString> names;
    if (directory.ReadDirectoryFileEntriesIntoStringList(names)) {
        for (const cRZString& name : names) {
            bool used = false;
            for (const auto& entry : mCacheEntries) {
                if (entry.first.str() == name.str()) {
                    used = true;
                    break;
                }
            }
            if (!used) RemoveCachedFile(name);
        }
    }
    return true;
}

// 0x100631418
bool NetworkFileCache::Shutdown() {
    if (mInitialized) {
        UpdateCacheIniFile();
        ClearCacheMap();
        mInitialized = false;
    }
    return true;
}

// 0x100631444: rewrites the INI file from the map. The file is removed first; entries without a file name are skipped
// (their RAM data is first written into a new cache file when a name can be had), and skipped entries stay in the map.
bool NetworkFileCache::UpdateCacheIniFile() {
    const cRZString section(kDefaultIniFileSection);
    const cRZString path = MakeCachePath(mCacheDirectory, mCacheIniFileName);
    cRZIniFile ini(path);
    cRZFile::Remove(path);
    std::uint32_t index = 0;
    for (auto& entry : mCacheEntries) {
        NetworkFileCacheInfo& info = entry.second;
        if (info.fileName.str().empty() && info.data != nullptr) {
            if (GetNewCacheFileName(entry.first, info.fileName) &&
                !WriteDataToFile(info.fileName, info.data->data, info.length)) {
                info.fileName.str().resize(0);
            }
        }
        if (!info.fileName.str().empty()) {
            cRZString keyName;
            keyName.Sprintf(kCacheEntryKeyFormat, index);
            cRZString value;
            value.Sprintf(kCacheEntryValueFormat, entry.first.ToChar(), info.fileName.ToChar(), info.length,
                          info.flags, info.expirationDuration, static_cast<int>(info.timeCreated),
                          static_cast<int>(info.lastAccessTime), static_cast<int>(info.expirationTime));
            ini.WriteEntry(section, keyName, value);
            ++index;
        }
    }
    return true;
}

// 0x1006317c4: releases the RAM copies and empties the map (the files are left alone).
void NetworkFileCache::ClearCacheMap() {
    for (auto& entry : mCacheEntries) {
        if (entry.second.data != nullptr) entry.second.data->Release();
    }
    mCacheEntries.clear();
}

// 0x1006317fa
std::uint32_t NetworkFileCache::AddRef() { return ++mRefCount; }

// 0x100631808: the last reference deletes the object through the deleting destructor (vtable slot +8).
std::uint32_t NetworkFileCache::Release() {
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// 0x100631824
bool NetworkFileCache::GetCacheDirectory(cRZString& out) {
    out.str() = mCacheDirectory.str();
    return true;
}

// 0x10063183c: empty refuses, a missing trailing '/' is added, the directory is created when needed.
bool NetworkFileCache::SetCacheDirectory(const cRZString& directory) {
    mCacheDirectory.str() = directory.str();
    if (mCacheDirectory.str().empty()) return false;
    if (mCacheDirectory.str().back() != '/') mCacheDirectory.str().push_back('/');
    if (!cRZDirectory::DoesDirectoryExist(mCacheDirectory) && !cRZDirectory::CreateDirectoryA(mCacheDirectory)) {
        return false;
    }
    return true;
}

// 0x1006318c0
bool NetworkFileCache::GetCacheIniFileName(cRZString& out) {
    out.str() = mCacheIniFileName.str();
    return true;
}

// 0x1006318d8: removes the old INI file (the current name in the current directory) before taking the new name.
bool NetworkFileCache::SetCacheIniFileName(const cRZString& iniFileName) {
    cRZString path;
    path.str() = mCacheDirectory.str();
    path.str().append(mCacheIniFileName.str());
    if (cRZFile::FileExists(path)) cRZFile::Remove(path);
    mCacheIniFileName.str() = iniFileName.str();
    return true;
}

// 0x1006319a8: stored only — no function of the class reads the RAM limit.
bool NetworkFileCache::SetRAMCacheSize(std::uint32_t bytes) {
    mRAMCacheSize = bytes;
    return true;
}

// 0x1006319b6: stored only — no function of the class reads the file limit.
bool NetworkFileCache::SetFileCacheSize(std::uint32_t bytes) {
    mFileCacheSize = bytes;
    return true;
}

// 0x1006319c4
bool NetworkFileCache::SetDefaultExpirationTime(std::uint32_t seconds) {
    mDefaultExpirationTime = seconds;
    return true;
}

// 0x1006319d2: true when the key is present and not expired. An expired entry is released (RAM data), its file is
// removed and the entry is erased.
bool NetworkFileCache::IsCachedDataPresent(const cRZString& key) {
    auto entry = mCacheEntries.find(key);
    if (entry == mCacheEntries.end()) return false;
    const std::int32_t now = static_cast<std::int32_t>(std::time(nullptr));
    if (now < static_cast<std::int32_t>(entry->second.expirationTime)) return true;
    if (entry->second.data != nullptr) entry->second.data->Release();
    RemoveCachedFile(entry->second.fileName);
    mCacheEntries.erase(entry);
    return false;
}

// 0x100631a3a: deletes "<cache dir><fileName>"; an empty name is refused (and nothing is attempted).
bool NetworkFileCache::RemoveCachedFile(const cRZString& fileName) {
    if (fileName.str().empty()) return false;
    const cRZString path = MakeCachePath(mCacheDirectory, fileName);
    cRZFile::Remove(path);
    return true;
}

// 0x100631afa
bool NetworkFileCache::IsCachedDataPresentAsRAMData(const cRZString& key) {
    auto entry = mCacheEntries.find(key);
    if (entry == mCacheEntries.end()) return false;
    const std::int32_t now = static_cast<std::int32_t>(std::time(nullptr));
    if (now < static_cast<std::int32_t>(entry->second.expirationTime)) return entry->second.data != nullptr;
    if (entry->second.data != nullptr) entry->second.data->Release();
    RemoveCachedFile(entry->second.fileName);
    mCacheEntries.erase(entry);
    return false;
}

// 0x100631b66
bool NetworkFileCache::IsCachedDataPresentAsFileData(const cRZString& key) {
    auto entry = mCacheEntries.find(key);
    if (entry == mCacheEntries.end()) return false;
    const std::int32_t now = static_cast<std::int32_t>(std::time(nullptr));
    if (now < static_cast<std::int32_t>(entry->second.expirationTime)) return !entry->second.fileName.str().empty();
    if (entry->second.data != nullptr) entry->second.data->Release();
    RemoveCachedFile(entry->second.fileName);
    mCacheEntries.erase(entry);
    return false;
}

// 0x100631be2: the key's bytes. A RAM entry is handed out with an extra reference; a file entry is read into a new
// CacheData (length refreshed from the file) before it is handed out.
bool NetworkFileCache::GetCachedDataAsData(const cRZString& key, CacheData** out) {
    ++mAccessCount;
    if (++mMaintenanceCount > kMaintenanceInterval) DoPeriodicCacheMaintenance();
    auto entry = mCacheEntries.find(key);
    if (entry == mCacheEntries.end()) return false;
    NetworkFileCacheInfo& info = entry->second;
    const std::int32_t now = static_cast<std::int32_t>(std::time(nullptr));
    if (now >= static_cast<std::int32_t>(info.expirationTime)) {
        if (info.data != nullptr) info.data->Release();
        RemoveCachedFile(info.fileName);
        mCacheEntries.erase(entry);
        return false;
    }
    if (out != nullptr) {
        if (info.data == nullptr) {
            const cRZString path = MakeCachePath(mCacheDirectory, info.fileName);
            cRZFile file(path);
            if (!file.Open(1, 2, 1)) {
                // `info.data` is known to be null here, so the original's Release is a no-op.
                cRZFile::Remove(path);
                mCacheEntries.erase(entry);
                return false;
            }
            const std::uint32_t length = file.Length();
            bool readOk = false;
            if (length != 0) {
                info.length = length;
                auto* data = new CacheData;
                data->data = new unsigned char[length];
                data->size = length;
                data->refCount = 0;
                info.data = data;
                data->refCount = static_cast<std::uint16_t>(data->refCount + 1);
                readOk = file.Read(data->data, length);
            }
            file.Close();
            if (!readOk) {
                if (info.data != nullptr) info.data->Release();
                cRZFile::Remove(path);
                mCacheEntries.erase(entry);
                return false;
            }
            info.data->refCount = static_cast<std::uint16_t>(info.data->refCount + 1);   // second reference: the caller
        } else {
            info.data->refCount = static_cast<std::uint16_t>(info.data->refCount + 1);
        }
        *out = info.data;
    }
    info.lastAccessTime = static_cast<std::uint32_t>(now);
    return true;
}

// 0x100631e60: drops every entry whose expiration time has passed (unsigned comparison here, signed in the getters).
void NetworkFileCache::DoPeriodicCacheMaintenance() {
    mMaintenanceCount = 0;
    for (auto entry = mCacheEntries.begin(); entry != mCacheEntries.end();) {
        const auto next = std::next(entry);   // the original keeps the next node before erasing this one
        NetworkFileCacheInfo& info = entry->second;
        const std::uint32_t now = static_cast<std::uint32_t>(std::time(nullptr));
        if (info.expirationTime <= now) {
            if (info.data != nullptr) info.data->Release();
            RemoveCachedFile(info.fileName);
            mCacheEntries.erase(entry);
        }
        entry = next;
    }
}

// 0x100631ecc: the entry's full path, with the RAM data written out to a new cache file first when it has no name.
// Only an output string triggers the write, and the result is "the entry has a file name", not "the file exists".
bool NetworkFileCache::GetCachedDataAsFile(const cRZString& key, cRZString* outPath) {
    ++mAccessCount;
    if (++mMaintenanceCount > kMaintenanceInterval) DoPeriodicCacheMaintenance();
    auto entry = mCacheEntries.find(key);
    if (entry == mCacheEntries.end()) return false;
    NetworkFileCacheInfo& info = entry->second;
    const std::int32_t now = static_cast<std::int32_t>(std::time(nullptr));
    if (now >= static_cast<std::int32_t>(info.expirationTime)) {
        if (info.data != nullptr) info.data->Release();
        RemoveCachedFile(info.fileName);
        mCacheEntries.erase(entry);
        return false;
    }
    if (outPath != nullptr) {
        if (info.data != nullptr && info.fileName.str().empty()) {
            if (GetNewCacheFileName(key, info.fileName) &&
                !WriteDataToFile(info.fileName, info.data->data, info.length)) {
                info.fileName.str().resize(0);
            }
        }
        outPath->str() = mCacheDirectory.str();
        outPath->str().append(info.fileName.str());
    }
    info.lastAccessTime = static_cast<std::uint32_t>(now);
    return !info.fileName.str().empty();
}

// 0x10063204a: "<cache dir><fileName>", opened for writing with create+truncate (cRZFile::Open(2, 3, 1)).
bool NetworkFileCache::WriteDataToFile(const cRZString& fileName, unsigned char* data, std::uint32_t length) {
    const cRZString path = MakeCachePath(mCacheDirectory, fileName);
    cRZFile file(path);
    if (!file.Open(2, 3, 1)) return false;
    const bool written = file.Write(data, length);
    file.Close();
    return written;
}

// 0x100632178: copies the record out (the cRZString and the four trailing qwords of the original).
bool NetworkFileCache::GetCachedFileInfo(const cRZString& key, NetworkFileCacheInfo& out) {
    auto entry = mCacheEntries.find(key);
    if (entry == mCacheEntries.end()) return false;
    const NetworkFileCacheInfo& info = entry->second;
    out.fileName = info.fileName;
    out.data = info.data;
    out.length = info.length;
    out.flags = info.flags;
    out.expirationDuration = info.expirationDuration;
    out.timeCreated = info.timeCreated;
    out.lastAccessTime = info.lastAccessTime;
    out.expirationTime = info.expirationTime;
    return true;
}

// 0x1006321ce: hands out the record inside the map (borrowed; erased with the entry).
bool NetworkFileCache::GetCachedFileInfo(const cRZString& key, NetworkFileCacheInfo** out) {
    auto entry = mCacheEntries.find(key);
    if (entry == mCacheEntries.end()) return false;
    if (out != nullptr) *out = &entry->second;
    return true;
}

// 0x100632202: the original builds a temporary cRZString and calls GetCachedDataAsFile(key, &temp) through the vtable
// slot at +0x88 (the decompiler lost the argument setup; see the disassembly 0x100632202..0x100632254).
bool NetworkFileCache::ForceCachedDataIntoFile(const cRZString& key) {
    cRZString path;
    return GetCachedDataAsFile(key, &path);
}

// 0x100632270
std::uint32_t NetworkFileCache::GetCachedDataSize(const cRZString& key) {
    NetworkFileCacheInfo info;
    if (!GetCachedFileInfo(key, info)) return 0;
    return info.length;
}

// 0x10063229a
std::uint32_t NetworkFileCache::GetTimeCreated(const cRZString& key) {
    NetworkFileCacheInfo info;
    if (!GetCachedFileInfo(key, info)) return 0;
    return info.timeCreated;
}

// 0x1006322c4: the original leaves the time() result in EAX and returns nothing.
void NetworkFileCache::GetTimeNow() { (void)std::time(nullptr); }

// 0x1006322d0: registers a file that either already lives in the cache directory (copyFile == false) or has to be
// copied into it under a new name (copyFile == true). See the header for the GetPathDirectory and GetNewCacheFileName
// quirks that decide which branch actually runs on macOS.
bool NetworkFileCache::AddNewCachedData(const cRZString& key, const cRZString& fileName, bool copyFile, bool flags,
                                        int expirationTime) {
    if (key.str().empty()) return false;
    ++mAccessCount;
    if (++mMaintenanceCount > kMaintenanceInterval) DoPeriodicCacheMaintenance();
    auto entry = mCacheEntries.find(key);
    if (entry != mCacheEntries.end()) {
        if (entry->second.data != nullptr) entry->second.data->Release();
        RemoveCachedFile(entry->second.fileName);
        mCacheEntries.erase(entry);
    }
    NetworkFileCacheInfo info;
    cRZFile::GetPathName(fileName, info.fileName, true);   // the bare name; the directory is not kept
    info.flags = flags ? 1u : 0u;
    if (expirationTime == -1) expirationTime = static_cast<int>(mDefaultExpirationTime);
    info.expirationDuration = static_cast<std::uint32_t>(expirationTime);
    const std::uint32_t now = TimeNow32();
    info.timeCreated = now;
    info.lastAccessTime = now;
    info.expirationTime = expirationTime < 0 ? kNeverExpires : now + static_cast<std::uint32_t>(expirationTime);
    bool needsCopy = copyFile;
    if (!needsCopy) {
        cRZString sourceDirectory;
        cRZFile::GetPathDirectory(fileName, sourceDirectory);
        sourceDirectory.MakeLower();
        cRZString cacheDirectory = mCacheDirectory;
        cacheDirectory.MakeLower();
        needsCopy = sourceDirectory.str() != cacheDirectory.str();
    }
    if (needsCopy) {
        if (GetNewCacheFileName(key, info.fileName)) {
            const cRZString copyPath = MakeCachePath(mCacheDirectory, info.fileName);
            if (!cRZFile::Copy(fileName, copyPath, true)) return false;
        }
        // else: the original keeps the bare source name and falls through to the length check below.
    }
    info.length = GetFileLength(info.fileName);
    if (info.length == 0 || info.length == 0xffffffffu) return false;
    if (mCacheEntries.find(key) == mCacheEntries.end()) mCacheEntries.emplace(key, info);
    return true;
}

// 0x100632b66: the length of "<cache dir><fileName>", 0 when the file cannot be opened for reading.
std::uint32_t NetworkFileCache::GetFileLength(const cRZString& fileName) {
    const cRZString path = MakeCachePath(mCacheDirectory, fileName);
    cRZFile file(path);
    std::uint32_t length = 0;
    if (file.Open(1, 2, 1)) {
        length = file.Length();
        file.Close();
    }
    return length;
}

// 0x100632c7e: registers a RAM copy. `copyData` takes a private copy of the bytes (otherwise the caller's buffer is
// handed to the CacheData and freed by it later — quirk of the original); `flags` only travels to the INI file.
bool NetworkFileCache::AddNewCachedData(const cRZString& key, const void* data, std::uint32_t length, bool copyData,
                                        bool flags, int expirationTime) {
    if (key.str().empty()) return false;
    ++mAccessCount;
    if (++mMaintenanceCount > kMaintenanceInterval) DoPeriodicCacheMaintenance();
    auto entry = mCacheEntries.find(key);
    if (entry != mCacheEntries.end()) {
        if (entry->second.data != nullptr) entry->second.data->Release();
        RemoveCachedFile(entry->second.fileName);
        mCacheEntries.erase(entry);
    }
    if (data == nullptr || length == 0) return false;
    auto* cacheData = new CacheData;
    cacheData->data = const_cast<unsigned char*>(static_cast<const unsigned char*>(data));
    cacheData->size = length;
    cacheData->refCount = 1;
    if (copyData) {
        auto* copy = new unsigned char[length];
        std::memcpy(copy, data, length);
        cacheData->data = copy;
    }
    NetworkFileCacheInfo info;
    info.data = cacheData;
    info.length = length;
    info.flags = flags ? 1u : 0u;
    if (expirationTime == -1) expirationTime = static_cast<int>(mDefaultExpirationTime);
    info.expirationDuration = static_cast<std::uint32_t>(expirationTime);
    const std::uint32_t now = TimeNow32();
    info.timeCreated = now;
    info.lastAccessTime = now;
    info.expirationTime = expirationTime < 0 ? kNeverExpires : now + static_cast<std::uint32_t>(expirationTime);
    if (mCacheEntries.find(key) == mCacheEntries.end()) mCacheEntries.emplace(key, info);
    return true;
}

// 0x10063326a
bool NetworkFileCache::RemoveCachedData(const cRZString& key) {
    ++mAccessCount;
    if (++mMaintenanceCount > kMaintenanceInterval) DoPeriodicCacheMaintenance();
    auto entry = mCacheEntries.find(key);
    if (entry != mCacheEntries.end()) {
        if (entry->second.data != nullptr) entry->second.data->Release();
        RemoveCachedFile(entry->second.fileName);
        mCacheEntries.erase(entry);
    }
    return true;
}

// 0x1006332e8: "<time><extension>.cache", one second back per attempt, accepted when cRZFile::Open(2, 0, 1) succeeds
// (O_WRONLY|O_EXCL without O_CREAT: on Darwin that only opens an existing file — see the header). 10000 attempts.
bool NetworkFileCache::GetNewCacheFileName(const cRZString& key, cRZString& outName) {
    std::int32_t stamp = static_cast<std::int32_t>(std::time(nullptr));
    const std::string extension = CacheFileExtension(key);
    for (std::uint32_t attempt = 0; attempt < kNewCacheNameAttempts; ++attempt) {
        outName.Sprintf(kCacheFileNameFormat, stamp, extension.c_str());
        const cRZString path = MakeCachePath(mCacheDirectory, outName);
        cRZFile file;
        file.SetPath(path);
        if (file.Open(2, 0, 1)) {
            file.Close();
            return true;
        }
        --stamp;
    }
    return false;
}

// 0x1006335d0: the same candidate search, but the output is the full path and the directory may be overridden (the
// class uses the cache directory when `directory` is null).
bool NetworkFileCache::GetNewCacheFilePath(const cRZString& key, cRZString& outPath, const cRZString* directory) {
    std::int32_t stamp = static_cast<std::int32_t>(std::time(nullptr));
    const std::string extension = CacheFileExtension(key);
    const cRZString& baseDirectory = directory != nullptr ? *directory : mCacheDirectory;
    cRZFile file;
    for (std::uint32_t attempt = 0; attempt < kNewCacheNameAttempts; ++attempt) {
        cRZString name;
        name.Sprintf(kCacheFileNameFormat, stamp, extension.c_str());
        outPath.str() = baseDirectory.str();
        outPath.str().append(name.str());
        file.SetPath(outPath);
        if (file.Open(2, 0, 1)) {
            file.Close();
            return true;
        }
        --stamp;
    }
    return false;
}

// 0x1006338b8: one "CacheEntryNNNNN=key,file,length,flags,duration,created,lastAccess,expiration" line. The value is
// split from the right into eight fields (a comma-less value or one with fewer than seven commas is ignored); the
// INI key itself is never used.
void NetworkFileCache::IniFileCallbackFunction(const cRZString& key, const cRZString& value, void* context) {
    (void)key;
    auto* self = static_cast<NetworkFileCache*>(context);
    if (value.str().empty()) return;
    cRZString fields[8];
    std::string rest = value.str();
    int index = 7;
    std::size_t position = rest.size();
    while (index > 0 && position > 0) {
        --position;
        if (rest[position] != ',') continue;
        fields[index].str() = rest.substr(position + 1);
        rest.erase(position);
        --index;
    }
    if (index != 0) return;
    fields[0].str() = rest;
    NetworkFileCacheInfo info;
    info.fileName = fields[1];
    info.data = nullptr;
    info.length = static_cast<std::uint32_t>(std::atoi(fields[2].ToChar()));
    info.flags = static_cast<std::uint32_t>(std::atoi(fields[3].ToChar()));
    info.expirationDuration = static_cast<std::uint32_t>(std::atoi(fields[4].ToChar()));
    info.timeCreated = static_cast<std::uint32_t>(std::atoi(fields[5].ToChar()));
    info.lastAccessTime = static_cast<std::uint32_t>(std::atoi(fields[6].ToChar()));
    info.expirationTime = static_cast<std::uint32_t>(std::atoi(fields[7].ToChar()));
    if (info.length > kMaxCacheEntryLength) {
        self->RemoveCachedFile(info.fileName);
        return;
    }
    const std::uint32_t now = TimeNow32();
    if (info.timeCreated > now) info.timeCreated = now;
    if (info.lastAccessTime > now) info.lastAccessTime = now;
    if (info.expirationTime <= now) {
        self->RemoveCachedFile(info.fileName);
        return;
    }
    const cRZString path = MakeCachePath(self->mCacheDirectory, info.fileName);
    if (!cRZFile::FileExists(path)) return;   // the file is gone: the entry is silently dropped
    if (self->mCacheEntries.find(fields[0]) == self->mCacheEntries.end()) {
        self->mCacheEntries.emplace(fields[0], info);
    }
}

}  // namespace sc4
