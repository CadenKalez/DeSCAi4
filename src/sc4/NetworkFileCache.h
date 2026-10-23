// NetworkFileCache (41 functions at 0x100630d70..0x1006341c7; vtable 0x100ad9d68, one class, no base) — the HTTP
// download cache behind cGZWWWSystem (source path .../GZFrame/GZWWW/h/NetworkFileCache.h).
//
// The cache maps a request key (the URL string the WWW system was asked for) to a NetworkFileCacheInfo record that
// holds either a RAM copy of the bytes (CacheData) or the name of a file inside the cache directory, plus the
// bookkeeping that is persisted in the cache INI file. The whole state is one std::unordered_map keyed by a
// cRZString with the custom NetworkFileCacheStringHash.
//
// Cache directory / INI file (original constants, read from the binary):
//   directory      = <temp dir>/ + "Maxis Network Cache/"   (0x1009f9d35; GetTempDirectory 0x1000379ae + append)
//   INI file name  = "FileCache.ini"                          (0x100b12f28 _kDefaultIniFileName)
//   INI section    = "Cache Entries"                          (0x100b12f30 _kDefaultIniFileSection)
//   file filter    = "*.cache"                                (0x100b12f40 _kSearchCachedFileExtension)
//   entry key      = "CacheEntry%05u"                         (0x1009f9d4f)
//   entry value    = "<key>,<file name>,<length>,<flags>,<duration>,<created>,<last access>,<expiration>"
//                    ("%s,%s,%u,%u,%u,%d,%d,%d", 0x1009f9d5e)
//   default sizes  = 3,000,000 file bytes / 500,000 RAM bytes, expiration 432,000 s (5 days)
//
// Object layout (0x9c bytes):
//   +0x00 vptr
//   +0x08 bool     mInitialized          (Init is a no-op once set; Shutdown clears it)
//   +0x0c uint32   mRefCount
//   +0x10 cRZString mCacheDirectory      (always ends in '/' once set)
//   +0x38 cRZString mCacheIniFileName    ("FileCache.ini")
//   +0x60 std::unordered_map<cRZString, NetworkFileCacheInfo> mCacheEntries
//   +0x88 uint32   mFileCacheSize        (3,000,000; stored but never read by any function)
//   +0x8c uint32   mRAMCacheSize         (500,000; stored but never read by any function)
//   +0x90 uint32   mDefaultExpirationTime(432,000 s; -1 in AddNewCachedData means "use this")
//   +0x94 uint32   mAccessCount          (every public access path increments it)
//   +0x98 uint32   mMaintenanceCount     (DoPeriodicCacheMaintenance runs when it passes 0x28 = 40)
// (the unordered_map fields are libc++'s +0x00 bucket list pointer / +0x08 bucket count / +0x10 first node /
//  +0x18 size / +0x20 max load factor = 1.0f)
//
// NetworkFileCacheInfo (0x48 bytes; in the original it lives in the hash node at node+0x38):
//   +0x00 cRZString fileName             (bare cache file name, or the source's bare name, see the quirks)
//   +0x28 CacheData* data                (null = file backed, non-null = RAM copy)
//   +0x30 uint32 length                  (bytes; refreshed from the file on the first RAM read)
//   +0x34 uint32 flags                   (copied through from the AddNewCachedData flag argument; never inspected)
//   +0x38 uint32 expirationDuration      (seconds requested; -1 was replaced by the default)
//   +0x3c uint32 timeCreated
//   +0x40 uint32 lastAccessTime
//   +0x44 uint32 expirationTime          (0x7fffffff when the duration was negative)
//
// Known quirks kept on purpose (asserted by scripts/tests/sc4_networkfilecache_test.cpp):
//  * GetNewCacheFileName/GetNewCacheFilePath verify a candidate name with cRZFile::Open(2, 0, 1), i.e. O_WRONLY|O_EXCL
//    without O_CREAT (0x1000361c8, creation mode 0 = 0x800). On Darwin that opens an *existing* file for writing, so a
//    name is only accepted when a file with that name already exists: the "new" name is the current time stamp, shifted
//    back one second at a time, of the first of 10000 candidates that already exists. The candidate file is created
//    empty in the PC original; here nothing is created.
//  * The cache file name extension is derived from the key: rfind('.'), accepted for 2..8 characters, every character
//    outside [0-9A-Za-z.] is erased, and ".cache" is appended unconditionally — so "x/a.jpg" asks for "<time>.jpg.cache"
//    and a key without a usable extension for "<time>.cache".
//  * AddNewCachedData(key, fileName, ...) only takes the "already in the cache directory" fast path when
//    GetPathDirectory(fileName) (lower-cased) equals the lower-cased cache directory. cRZFile::GetPathDirectory always
//    returns an empty string (0x100038466, see cRZFile.h), so on macOS the fast path never triggers and `copyFile`
//    decides: true always copies, false copies only when the directories differ (i.e. always).
//  * If GetNewCacheFileName fails, AddNewCachedData falls through with the *bare source file name* recorded, and then
//    fails the GetFileLength check (the file is not in the cache directory), returning false. Nothing is inserted.
//  * The source name recorded by the copy path is the result of GetPathName (name + extension, no directory).
//  * RemoveUnusedCachedFiles compares the directory's *.cache file *names* against the cache *keys* (URLs), so every
//    real cache file is deleted at start-up unless a URL happens to equal a file name.
//  * The size limits (SetFileCacheSize/SetRAMCacheSize) are never enforced; the load factor is only used for the
//    map's bucket growth (the original's rehash calls are the std::unordered_map insert path).
//  * IsCachedDataPresent/AsRAMData/AsFileData/GetCachedData* compare the expiration with a *signed* 32-bit compare,
//    while DoPeriodicCacheMaintenance uses an unsigned one (0x100631e60).
//  * An expired entry is always dropped: the RAM data is released, the file is removed and the map entry erased.
//  * GetCachedDataAsData/GetCachedDataAsFile count an access before looking the key up, and GetCachedDataAsFile only
//    materialises a file (and only reports success) when the caller passed an output string.
//  * The INI reader ignores the "CacheEntry%05u" key and uses the first value field as the cache key; an entry needs
//    all seven commas, a length <= 4,000,000, an unexpired <expiration> and an existing file, and it never replaces an
//    entry that is already in the map. Bad lengths and expired entries *delete* the file they name
//    (RemoveCachedFile), and an entry whose file is missing is silently dropped.
//  * The INI writer skips entries without a file name and does not erase them; it writes the RAM data of such an entry
//    to a new cache file first (which needs the GetNewCacheFileName quirk above to find a name).
//  * GetNewCacheFilePath exists only in the vtable; nothing in this class calls it. It writes the full path
//    (directory + name) to the output string instead of just the name.
//  * Port deviation: the original's map is libc++'s unordered_map with the same hasher/equality, so the *iteration
//    order* of the two implementations can differ. Only the "CacheEntry%05u" numbering in the INI file, the order in
//    which unused files are scanned and the order in which maintenance drops entries depend on it; no function's
//    result does.
//
// Seams: the file system is reached only through the recovered cRZFile (files + the static path/existence helpers),
// cRZDirectory (the "*.cache" listing) and cRZIniFile (the "Cache Entries" section). time() is called directly
// (0x10092b6cc). No unrecovered neighbour is involved, so there is no I*Host interface in this class.
#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <unordered_map>

#include "rz/cRZString.h"
#include "sc4/MiscTypes.h"

namespace sc4 {

// Hasher of the original map (NetworkFileCacheStringHash, 0x100634634 callers): h = h * 2 ^ (signed char) for every
// byte of the string, starting at 0 (so the empty string hashes to 0).
struct NetworkFileCacheStringHash {
    std::size_t operator()(const cRZString& text) const {
        std::uint64_t hash = 0;
        for (char c : text.str()) {
            hash = hash * 2 ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<signed char>(c)));
        }
        return static_cast<std::size_t>(hash);
    }
};

// std::equal_to<cRZString> of the original: plain byte-for-byte equality of the embedded std::string.
struct NetworkFileCacheStringEqual {
    bool operator()(const cRZString& a, const cRZString& b) const { return a.str() == b.str(); }
};

struct NetworkFileCacheInfo {
    cRZString fileName;                      // +0x00
    CacheData* data = nullptr;               // +0x28
    std::uint32_t length = 0;                // +0x30
    std::uint32_t flags = 0;                 // +0x34
    std::uint32_t expirationDuration = 0;    // +0x38
    std::uint32_t timeCreated = 0;           // +0x3c
    std::uint32_t lastAccessTime = 0;        // +0x40
    std::uint32_t expirationTime = 0;        // +0x44
};

class NetworkFileCache {
public:
    // ctor 0x100630d70 (the complete-object ctor 0x100630ebc just forwards to it), dtor 0x100630ec6/0x100630f4e/
    // 0x100630f58.
    NetworkFileCache();
    virtual ~NetworkFileCache();

    // --- vtable, in slot order (0x100ad9d68: 0x10 Init, 0x18 Shutdown, 0x20 AddRef, 0x28 Release, ...) ---
    virtual bool Init();                                                     // 0x100630f86
    virtual bool Shutdown();                                                 // 0x100631418
    virtual std::uint32_t AddRef();                                          // 0x1006317fa
    virtual std::uint32_t Release();                                         // 0x100631808
    virtual bool GetCacheDirectory(cRZString& out);                          // 0x100631824
    virtual bool SetCacheDirectory(const cRZString& directory);              // 0x10063183c
    virtual bool GetCacheIniFileName(cRZString& out);                        // 0x1006318c0
    virtual bool SetCacheIniFileName(const cRZString& iniFileName);          // 0x1006318d8
    virtual bool SetRAMCacheSize(std::uint32_t bytes);                       // 0x1006319a8
    virtual bool SetFileCacheSize(std::uint32_t bytes);                      // 0x1006319b6
    virtual bool SetDefaultExpirationTime(std::uint32_t seconds);            // 0x1006319c4
    virtual bool IsCachedDataPresent(const cRZString& key);                 // 0x1006319d2
    virtual bool IsCachedDataPresentAsRAMData(const cRZString& key);         // 0x100631afa
    virtual bool IsCachedDataPresentAsFileData(const cRZString& key);        // 0x100631b66
    virtual bool GetCachedDataAsData(const cRZString& key, CacheData** out); // 0x100631be2
    virtual bool GetCachedDataAsFile(const cRZString& key, cRZString* outPath);   // 0x100631ecc
    virtual bool GetCachedFileInfo(const cRZString& key, NetworkFileCacheInfo& out);    // 0x100632178
    virtual bool GetCachedFileInfo(const cRZString& key, NetworkFileCacheInfo** out);   // 0x1006321ce
    virtual bool ForceCachedDataIntoFile(const cRZString& key);              // 0x100632202
    virtual std::uint32_t GetCachedDataSize(const cRZString& key);           // 0x100632270
    virtual std::uint32_t GetTimeCreated(const cRZString& key);              // 0x10063229a
    virtual void GetTimeNow();                                               // 0x1006322c4
    virtual bool AddNewCachedData(const cRZString& key, const cRZString& fileName, bool copyFile, bool flags,
                                  int expirationTime);                       // 0x1006322d0
    virtual bool AddNewCachedData(const cRZString& key, const void* data, std::uint32_t length, bool copyData,
                                  bool flags, int expirationTime);           // 0x100632c7e
    virtual bool RemoveCachedData(const cRZString& key);                     // 0x10063326a
    virtual bool GetNewCacheFileName(const cRZString& key, cRZString& outName);           // 0x1006332e8
    virtual bool GetNewCacheFilePath(const cRZString& key, cRZString& outPath,
                                     const cRZString* directory);            // 0x1006335d0

    // --- non-virtual internals ---
    bool ReadCacheIniFile();                                              // 0x100631004
    bool RemoveUnusedCachedFiles();                                       // 0x1006311ba
    bool UpdateCacheIniFile();                                            // 0x100631444
    void ClearCacheMap();                                                 // 0x1006317c4
    bool RemoveCachedFile(const cRZString& fileName);                     // 0x100631a3a
    void DoPeriodicCacheMaintenance();                                    // 0x100631e60
    bool WriteDataToFile(const cRZString& fileName, unsigned char* data, std::uint32_t length);   // 0x10063204a
    std::uint32_t GetFileLength(const cRZString& fileName);               // 0x100632b66
    // cRZIniFile::EnumEntries callback; `key` ("CacheEntry%05u") is ignored, `context` is the cache object.
    static void IniFileCallbackFunction(const cRZString& key, const cRZString& value, void* context);   // 0x1006338b8

    // Test/diagnostic access to the original's fields (the game reads them through the vtable only).
    using EntryMap = std::unordered_map<cRZString, NetworkFileCacheInfo, NetworkFileCacheStringHash,
                                        NetworkFileCacheStringEqual>;
    const EntryMap& Entries() const { return mCacheEntries; }
    EntryMap& Entries() { return mCacheEntries; }
    bool IsInitialized() const { return mInitialized; }
    std::uint32_t ReferenceCount() const { return mRefCount; }
    std::uint32_t AccessCount() const { return mAccessCount; }
    std::uint32_t FileCacheSize() const { return mFileCacheSize; }
    std::uint32_t RAMCacheSize() const { return mRAMCacheSize; }
    std::uint32_t DefaultExpirationTime() const { return mDefaultExpirationTime; }

private:
    bool mInitialized = false;                        // +0x08
    std::uint32_t mRefCount = 0;                      // +0x0c
    cRZString mCacheDirectory;                        // +0x10
    cRZString mCacheIniFileName;                      // +0x38
    EntryMap mCacheEntries;                           // +0x60
    std::uint32_t mFileCacheSize = 0;                 // +0x88
    std::uint32_t mRAMCacheSize = 0;                  // +0x8c
    std::uint32_t mDefaultExpirationTime = 0;         // +0x90
    std::uint32_t mAccessCount = 0;                   // +0x94
    std::uint32_t mMaintenanceCount = 0;              // +0x98
};

}  // namespace sc4
