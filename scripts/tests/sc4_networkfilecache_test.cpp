// NetworkFileCache (41 functions) — behaviour, INI round-trip and the quirks kept (see src/sc4/NetworkFileCache.h).
// The cache reaches the file system through the recovered cRZFile / cRZDirectory / cRZIniFile classes, so the test
// works on a real temporary directory (mkdtemp) instead of a fake file system, exactly like the game does.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

#include <unistd.h>

#include "sc4/NetworkFileCache.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

using namespace sc4;

// The record layouts the original stores in its hash node (NetworkFileCacheInfo starts at node+0x38, the key cRZString
// at node+0x10) and the 16-byte CacheData buffer holder.
static_assert(sizeof(CacheData) == 0x10, "CacheData is the original's 0x10-byte holder");
static_assert(offsetof(CacheData, size) == 0x8, "CacheData::size lives at +8");
static_assert(offsetof(CacheData, refCount) == 0xc, "CacheData::refCount lives at +0xc");
static_assert(sizeof(NetworkFileCacheInfo) == 0x48, "NetworkFileCacheInfo is 0x48 bytes");

namespace {

std::string TimeStamp() { return std::to_string(static_cast<long>(std::time(nullptr))); }

void WriteFile(const std::string& path, const std::string& content) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("cannot write %s\n", path.c_str()); ++g_fail; return; }
    if (!content.empty()) std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
}

std::string ReadFile(const std::string& path) {
    std::string out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    char buffer[256];
    std::size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) != 0) out.append(buffer, n);
    std::fclose(f);
    return out;
}

bool Exists(const std::string& path) { return std::filesystem::exists(path); }

std::uint32_t Length(const std::string& text) { return static_cast<std::uint32_t>(text.size()); }

// NetworkFileCacheInfo holds a cRZString, so offsetof would be non-standard; measure the offsets on a live object.
std::size_t OffsetOf(const void* base, const void* member) {
    return static_cast<std::size_t>(static_cast<const unsigned char*>(member) -
                                    static_cast<const unsigned char*>(base));
}

}  // namespace

int main() {
    char temporary[] = "/tmp/nfc_test_XXXXXX";
    if (mkdtemp(temporary) == nullptr) { std::printf("FAIL mkdtemp\n"); return 1; }
    const std::string root = temporary;
    const std::string cacheDirectory = root + "/cache";
    const std::string sourceDirectory = root + "/source";
    std::filesystem::create_directories(cacheDirectory);
    std::filesystem::create_directories(sourceDirectory);
    const std::string iniPath = cacheDirectory + "/FileCache.ini";
    // The candidate names the cache derives from a key; pinned to the time stamp this test starts with.
    const std::string stamp = TimeStamp();

    {
        NetworkFileCacheInfo probe;
        CHECK(OffsetOf(&probe, &probe.fileName) == 0x00);
        CHECK(OffsetOf(&probe, &probe.data) == 0x28);
        CHECK(OffsetOf(&probe, &probe.length) == 0x30);
        CHECK(OffsetOf(&probe, &probe.flags) == 0x34);
        CHECK(OffsetOf(&probe, &probe.expirationDuration) == 0x38);
        CHECK(OffsetOf(&probe, &probe.timeCreated) == 0x3c);
        CHECK(OffsetOf(&probe, &probe.lastAccessTime) == 0x40);
        CHECK(OffsetOf(&probe, &probe.expirationTime) == 0x44);
    }

    NetworkFileCache* cache = new NetworkFileCache;

    // ---------------------------------------------------------------- construction and the plain accessors
    CHECK(cache->ReferenceCount() == 0);
    CHECK(cache->AddRef() == 1);
    CHECK(cache->AddRef() == 2);
    CHECK(cache->Release() == 1);
    CHECK(cache->ReferenceCount() == 1);
    CHECK(!cache->IsInitialized());
    CHECK(cache->FileCacheSize() == 3000000u);
    CHECK(cache->RAMCacheSize() == 500000u);
    CHECK(cache->DefaultExpirationTime() == 432000u);
    cRZString text;
    CHECK(cache->GetCacheDirectory(text) && text.str().empty());
    CHECK(cache->GetCacheIniFileName(text) && text.str() == "FileCache.ini");
    CHECK(cache->SetRAMCacheSize(1234) && cache->RAMCacheSize() == 1234);
    CHECK(cache->SetFileCacheSize(5678) && cache->FileCacheSize() == 5678);
    CHECK(cache->SetDefaultExpirationTime(432000) && cache->DefaultExpirationTime() == 432000);
    // The size limits are stored only: nothing in the class reads them.
    CHECK(cache->SetRAMCacheSize(500000) && cache->SetFileCacheSize(3000000));

    // ---------------------------------------------------------------- SetCacheDirectory
    CHECK(!cache->SetCacheDirectory(cRZString("")));                      // empty is refused
    CHECK(cache->SetCacheDirectory(cRZString(cacheDirectory.c_str())));    // missing '/' is appended
    CHECK(cache->GetCacheDirectory(text) && text.str() == cacheDirectory + "/");
    CHECK(cache->SetCacheDirectory(cRZString((cacheDirectory + "/").c_str())));
    CHECK(cache->GetCacheDirectory(text) && text.str() == cacheDirectory + "/");   // no double separator
    CHECK(cache->SetCacheDirectory(cRZString((root + "/fresh/deeper").c_str())));  // created on demand
    CHECK(Exists(root + "/fresh/deeper"));
    CHECK(cache->SetCacheDirectory(cRZString(cacheDirectory.c_str())));

    // ---------------------------------------------------------------- Init
    CHECK(cache->Init());
    CHECK(cache->IsInitialized());
    CHECK(cache->Init());                                                  // second call is a no-op
    CHECK(!Exists(iniPath));                                               // no INI file yet

    // ---------------------------------------------------------------- RAM data
    const cRZString ramKey("http://example.com/ram.txt");
    const std::string payload = "hello cache";
    CHECK(!cache->AddNewCachedData(cRZString(""), payload.data(), 5, true, false, -1));      // empty key refused
    CHECK(!cache->AddNewCachedData(ramKey, nullptr, 5, true, false, -1));             // null data refused
    CHECK(!cache->AddNewCachedData(ramKey, payload.data(), 0, true, false, -1));             // zero length refused
    CHECK(cache->AddNewCachedData(ramKey, payload.data(), Length(payload), true, true, -1));
    CHECK(cache->IsCachedDataPresent(ramKey));
    CHECK(cache->IsCachedDataPresentAsRAMData(ramKey));
    CHECK(!cache->IsCachedDataPresentAsFileData(ramKey));
    CHECK(cache->GetCachedDataSize(ramKey) == Length(payload));
    CHECK(cache->GetTimeCreated(ramKey) != 0 && cache->GetTimeCreated(ramKey) <= static_cast<std::uint32_t>(std::time(nullptr)));
    CHECK(cache->GetCachedDataSize(cRZString("http://example.com/missing")) == 0);
    CHECK(cache->GetTimeCreated(cRZString("http://example.com/missing")) == 0);
    NetworkFileCacheInfo info;
    CHECK(cache->GetCachedFileInfo(ramKey, info));
    CHECK(info.length == Length(payload));
    CHECK(info.flags == 1);                                                // the flag argument travels through
    CHECK(info.expirationDuration == 432000u);                             // -1 became the default
    CHECK(info.timeCreated == info.lastAccessTime);
    CHECK(info.expirationTime == info.timeCreated + 432000u);
    CHECK(info.data != nullptr && info.data->size == Length(payload) && info.data->refCount == 1);
    NetworkFileCacheInfo* borrowed = nullptr;
    CHECK(cache->GetCachedFileInfo(ramKey, &borrowed) && borrowed == &cache->Entries().find(ramKey)->second);
    CHECK(!cache->GetCachedFileInfo(cRZString("http://example.com/missing"), info));
    const std::uint32_t accessesBefore = cache->AccessCount();
    CacheData* data = nullptr;
    CHECK(cache->GetCachedDataAsData(ramKey, &data));
    CHECK(data == info.data);
    CHECK(data->refCount == 2);                                            // one for the map, one for the caller
    CHECK(data->size == Length(payload) && std::memcmp(data->data, payload.data(), Length(payload)) == 0);
    CHECK(cache->AccessCount() == accessesBefore + 1);
    data->Release();
    CHECK(info.data->refCount == 1);
    // GetCachedDataAsData with a null output only updates the access time.
    CHECK(cache->GetCachedDataAsData(ramKey, nullptr));
    // The RAM entry has no file name, so the file accessor only reports failure and writes nothing.
    CHECK(!cache->GetCachedDataAsFile(ramKey, nullptr));
    CHECK(cache->GetCachedFileInfo(ramKey, info) && info.fileName.str().empty());
    // ForceCachedDataIntoFile builds its own output string, so it does try to materialise a cache file. The candidate
    // name that GetNewCacheFileName generated is left in the entry even when the candidate search fails, so the call
    // reports success with a name whose file was never created (quirk kept).
    CHECK(!Exists(cacheDirectory + "/" + stamp + ".txt.cache"));
    CHECK(cache->ForceCachedDataIntoFile(ramKey));
    CHECK(cache->GetCachedFileInfo(ramKey, info));
    CHECK(!info.fileName.str().empty() && info.fileName.str() != stamp + ".txt.cache");   // last failed attempt
    CHECK(!Exists(cacheDirectory + "/" + stamp + ".txt.cache"));
    // A fresh entry does write the data out once the candidate name exists.
    CHECK(cache->RemoveCachedData(ramKey));
    CHECK(cache->AddNewCachedData(ramKey, payload.data(), Length(payload), true, true, -1));
    WriteFile(cacheDirectory + "/" + stamp + ".txt.cache", "");
    CHECK(cache->ForceCachedDataIntoFile(ramKey));
    CHECK(cache->GetCachedFileInfo(ramKey, info) && info.fileName.str() == stamp + ".txt.cache");
    CHECK(ReadFile(cacheDirectory + "/" + stamp + ".txt.cache") == payload);

    // ---------------------------------------------------------------- a RAM entry that takes over caller memory
    const cRZString borrowedKey("http://example.com/borrowed");
    unsigned char* callerBuffer = new unsigned char[4];
    std::memcpy(callerBuffer, "abcd", 4);
    CHECK(cache->AddNewCachedData(borrowedKey, callerBuffer, 4, false, false, -1));
    CacheData* borrowedData = nullptr;
    CHECK(cache->GetCachedDataAsData(borrowedKey, &borrowedData));
    CHECK(borrowedData->data == callerBuffer);                             // no copy: the CacheData owns the buffer
    borrowedData->Release();
    CHECK(cache->RemoveCachedData(borrowedKey));                           // releases the buffer (and nothing else)
    CHECK(!cache->IsCachedDataPresent(borrowedKey));

    // ---------------------------------------------------------------- file data: the copy path
    // GetNewCacheFileName accepts a name only when cRZFile::Open(2, 0, 1) succeeds, i.e. O_WRONLY|O_EXCL without
    // O_CREAT, which on Darwin only opens an existing file: pre-create the candidate.
    const std::string sourceFile = sourceDirectory + "/photo.jpg";
    WriteFile(sourceFile, "JPEGDATA");
    const std::string jpgCandidate = cacheDirectory + "/" + stamp + ".jpg.cache";
    WriteFile(jpgCandidate, "");
    const cRZString fileKey("http://example.com/photos/photo.jpg");
    CHECK(cache->AddNewCachedData(fileKey, cRZString(sourceFile.c_str()), true, false, -1));
    CHECK(cache->IsCachedDataPresent(fileKey));
    CHECK(cache->IsCachedDataPresentAsFileData(fileKey));
    CHECK(!cache->IsCachedDataPresentAsRAMData(fileKey));
    CHECK(cache->GetCachedDataSize(fileKey) == 8);
    CHECK(cache->GetCachedFileInfo(fileKey, info));
    CHECK(info.fileName.str() == stamp + ".jpg.cache");                    // GetPathName dropped the directory
    CHECK(info.flags == 0);
    CHECK(info.data == nullptr);
    CHECK(ReadFile(jpgCandidate) == "JPEGDATA");                           // the copy landed in the cache directory
    cRZString cachePath;
    CHECK(cache->GetCachedDataAsFile(fileKey, &cachePath));
    CHECK(cachePath.str() == jpgCandidate);
    CHECK(cache->ForceCachedDataIntoFile(fileKey));
    // Reading a file entry back loads it into a CacheData and refreshes the length from the file.
    CacheData* fileData = nullptr;
    CHECK(cache->GetCachedDataAsData(fileKey, &fileData));
    CHECK(fileData->size == 8 && std::memcmp(fileData->data, "JPEGDATA", 8) == 0);
    CHECK(fileData->refCount == 2);
    fileData->Release();
    CHECK(cache->IsCachedDataPresentAsRAMData(fileKey));                   // now RAM-backed as well

    // The copy path fails when no candidate cache file exists: GetNewCacheFileName finds no name, the recorded name is
    // the source's bare name, and GetFileLength cannot find it in the cache directory.
    const std::string sourceFile2 = sourceDirectory + "/notes.zzz";
    WriteFile(sourceFile2, "abc");
    const cRZString missingKey("http://example.com/notes.zzz");
    CHECK(!cache->AddNewCachedData(missingKey, cRZString(sourceFile2.c_str()), true, false, -1));
    CHECK(!cache->IsCachedDataPresent(missingKey));
    CHECK(cache->Entries().find(missingKey) == cache->Entries().end());

    // copyFile == false still takes the copy path, because cRZFile::GetPathDirectory always reports an empty
    // directory (see cRZFile.h) and the cache directory is never empty.
    const std::string inCacheSource = cacheDirectory + "/already.bmp";
    WriteFile(inCacheSource, "BMP");
    const std::string bmpCandidate = cacheDirectory + "/" + stamp + ".bmp.cache";
    WriteFile(bmpCandidate, "");
    const cRZString bmpKey("http://example.com/already.bmp");
    CHECK(cache->AddNewCachedData(bmpKey, cRZString(inCacheSource.c_str()), false, true, -1));
    CHECK(cache->GetCachedFileInfo(bmpKey, info) && info.fileName.str() == stamp + ".bmp.cache");
    CHECK(ReadFile(bmpCandidate) == "BMP");

    // ---------------------------------------------------------------- expiration
    const cRZString expiredKey("http://example.com/expired");
    CHECK(cache->AddNewCachedData(expiredKey, payload.data(), 5, true, false, 0));   // expires "now": already gone
    CHECK(!cache->IsCachedDataPresent(expiredKey));                          // dropped on the first look
    CHECK(cache->Entries().find(expiredKey) == cache->Entries().end());
    const cRZString foreverKey("http://example.com/forever");
    CHECK(cache->AddNewCachedData(foreverKey, payload.data(), 5, true, false, -5)); // any negative but -1 never expires
    CHECK(cache->GetCachedFileInfo(foreverKey, info) && info.expirationTime == 0x7fffffffu);
    CHECK(info.expirationDuration == static_cast<std::uint32_t>(-5));
    CHECK(cache->IsCachedDataPresent(foreverKey));
    CHECK(cache->RemoveCachedData(foreverKey));
    CHECK(!cache->IsCachedDataPresent(foreverKey));
    CHECK(!cache->RemoveCachedFile(cRZString("")));                          // empty name refused

    // ---------------------------------------------------------------- GetNewCacheFileName extension rules
    struct NameCase { const char* key; const char* suffix; };
    static const NameCase kNameCases[] = {
        {"http://example.com/a.png", ".png.cache"},
        {"http://example.com/noextension", ".cache"},
        {"http://example.com/x.a-b", ".ab.cache"},        // '-' is erased
        {"http://example.com/x.ab", ".ab.cache"},         // two characters are accepted
        {"http://example.com/x.abcdefghi", ".cache"},     // a nine-character tail is too long
        {"http://example.com/x.abcdefg", ".abcdefg.cache"},
    };
    for (const NameCase& nameCase : kNameCases) {
        const std::string candidate = cacheDirectory + "/" + stamp + nameCase.suffix;
        WriteFile(candidate, "candidate");
        cRZString name;
        CHECK(cache->GetNewCacheFileName(cRZString(nameCase.key), name));
        CHECK(name.str() == stamp + nameCase.suffix);
    }
    // A key the candidate search cannot satisfy: 10000 failed opens, then false. The output keeps the last attempt.
    {
        cRZString name;
        CHECK(!cache->GetNewCacheFileName(cRZString("http://example.com/absent.qwert"), name));
        const std::string suffix = ".qwert.cache";
        CHECK(name.str().size() > suffix.size() &&
              name.str().compare(name.str().size() - suffix.size(), suffix.size(), suffix) == 0);
    }
    // GetNewCacheFilePath returns the full path, with the cache directory as the default base.
    {
        const cRZString baseDirectory((sourceDirectory + "/").c_str());
        const std::string pathCandidate = sourceDirectory + "/" + stamp + ".bin.cache";
        WriteFile(pathCandidate, "path");
        cRZString path;
        CHECK(cache->GetNewCacheFilePath(cRZString("http://example.com/x.bin"), path, &baseDirectory));
        CHECK(path.str() == pathCandidate);
        const std::string defaultCandidate = cacheDirectory + "/" + stamp + ".dat.cache";
        WriteFile(defaultCandidate, "path");
        CHECK(cache->GetNewCacheFilePath(cRZString("http://example.com/y.dat"), path, nullptr));
        CHECK(path.str() == defaultCandidate);
    }

    // ---------------------------------------------------------------- INI round-trip
    CHECK(cache->Shutdown());                                              // writes FileCache.ini, drops the map
    CHECK(!cache->IsInitialized());
    CHECK(cache->Entries().empty());                                       // ClearCacheMap
    CHECK(Exists(iniPath));
    const std::string ini = ReadFile(iniPath);
    CHECK(ini.find("[Cache Entries]") != std::string::npos);
    CHECK(ini.find("CacheEntry00000=") != std::string::npos);
    CHECK(ini.find(ramKey.ToChar()) != std::string::npos);
    CHECK(ini.find(fileKey.ToChar()) != std::string::npos);
    CHECK(ini.find(stamp + ".jpg.cache") != std::string::npos);
    CHECK(ini.find("8,0,432000") != std::string::npos || ini.find("8,1,432000") != std::string::npos);

    // ---------------------------------------------------------------- reload: the INI rebuilds the entry
    WriteFile(cacheDirectory + "/stray.cache", "junk");
    NetworkFileCache* reloaded = new NetworkFileCache;
    CHECK(reloaded->SetCacheDirectory(cRZString(cacheDirectory.c_str())));
    CHECK(reloaded->Init());
    CHECK(reloaded->IsCachedDataPresentAsFileData(fileKey));
    CHECK(reloaded->GetCachedDataSize(fileKey) == 8);
    CHECK(reloaded->IsCachedDataPresentAsFileData(ramKey));
    CHECK(reloaded->GetCachedDataSize(ramKey) == Length(payload));
    CHECK(reloaded->GetCachedFileInfo(ramKey, info) && info.expirationDuration == 432000u);
    CHECK(reloaded->GetCachedFileInfo(fileKey, info));
    CHECK(info.flags == 0 && info.length == 8);
    CHECK(info.timeCreated != 0 && info.timeCreated <= static_cast<std::uint32_t>(std::time(nullptr)));
    CHECK(info.lastAccessTime <= static_cast<std::uint32_t>(std::time(nullptr)));
    CHECK(info.expirationTime == info.timeCreated + info.expirationDuration);
    // RemoveUnusedCachedFiles compares the "*.cache" file names with the cache keys (URLs), so it deletes every real
    // cache file at start-up — including the entry's file and the stray one — while the entries stay.
    CHECK(!Exists(cacheDirectory + "/stray.cache"));
    CHECK(!Exists(jpgCandidate));
    CHECK(reloaded->IsCachedDataPresentAsFileData(fileKey));
    CacheData* gone = nullptr;
    CHECK(!reloaded->GetCachedDataAsData(fileKey, &gone));                 // the file is gone: the entry is dropped
    CHECK(!reloaded->IsCachedDataPresent(fileKey));

    // ---------------------------------------------------------------- INI reader edge cases
    // Fields: key, file name, length, flags, duration, created, last access, expiration. The reader needs all seven
    // commas, rejects a length above 4,000,000, deletes the file of an expired or oversized entry, drops an entry whose
    // file is missing, clamps created/last access to "now", and never replaces a key that is already in the map.
    const std::string iniDirectory = root + "/ini";
    std::filesystem::create_directories(iniDirectory);
    WriteFile(iniDirectory + "/good.cache", "GOOD");
    WriteFile(iniDirectory + "/big.cache", "BIG");
    WriteFile(iniDirectory + "/expired.cache", "EXP");
    const std::uint32_t nowSeconds = static_cast<std::uint32_t>(std::time(nullptr));
    const std::string nowText = std::to_string(nowSeconds);
    const std::string present = std::to_string(nowSeconds + 1000);
    const std::string past = std::to_string(nowSeconds - 10);
    const std::string future = std::to_string(nowSeconds + 5000);
    std::string iniText = "[Cache Entries]\n";
    iniText += "CacheEntry00000=k1,good.cache,4,7,100," + nowText + "," + nowText + "," + present + "\n";
    iniText += "CacheEntry00001=k2,big.cache,4000001,0,100," + nowText + "," + nowText + "," + present + "\n";
    iniText += "CacheEntry00002=k3,expired.cache,1,0,100," + nowText + "," + nowText + "," + past + "\n";
    iniText += "CacheEntry00003=k4,missing.cache,1,0,100," + nowText + "," + nowText + "," + present + "\n";
    iniText += "CacheEntry00004=k5,good.cache,4,0,100\n";
    iniText += "CacheEntry00005=k6\n";
    iniText += "CacheEntry00006=k7,good.cache,4,7,100," + nowText + "," + nowText + "\n";
    iniText += "CacheEntry00007=k1,good.cache,4,9,100," + nowText + "," + nowText + "," + present + "\n";
    iniText += "CacheEntry00008=k8,good.cache,4,0,100," + future + "," + future + "," + present + "\n";
    iniText += "CacheEntry00009=k9,good.cache,4,0,100," + nowText + "," + nowText + ",4000000000\n";
    WriteFile(iniDirectory + "/FileCache.ini", iniText);
    NetworkFileCache* reader = new NetworkFileCache;
    CHECK(reader->SetCacheDirectory(cRZString(iniDirectory.c_str())));
    CHECK(reader->Init());
    CHECK(reader->GetCachedFileInfo(cRZString("k1"), info) && info.flags == 7 && info.length == 4);
    CHECK(reader->GetCachedFileInfo(cRZString("k8"), info));
    CHECK(info.flags == 0);
    CHECK(info.timeCreated <= static_cast<std::uint32_t>(std::time(nullptr)));   // the future value was clamped
    CHECK(info.lastAccessTime <= static_cast<std::uint32_t>(std::time(nullptr)));
    CHECK(info.expirationTime == nowSeconds + 1000);                             // but the expiration was not
    CHECK(!reader->GetCachedFileInfo(cRZString("k2"), info));                    // length above 4,000,000
    CHECK(!Exists(iniDirectory + "/big.cache"));                                 // and its file was deleted
    CHECK(!reader->GetCachedFileInfo(cRZString("k3"), info));                    // expired
    CHECK(!Exists(iniDirectory + "/expired.cache"));                             // and its file was deleted
    CHECK(!reader->GetCachedFileInfo(cRZString("k4"), info));                    // file missing: silently dropped
    CHECK(!reader->GetCachedFileInfo(cRZString("k5"), info));                    // too few fields
    CHECK(!reader->GetCachedFileInfo(cRZString("k6"), info));                    // no comma at all
    CHECK(!reader->GetCachedFileInfo(cRZString("k7"), info));                    // six commas only
    CHECK(reader->GetCachedFileInfo(cRZString("k9"), info));                     // accepted: unsigned compare
    CHECK(info.expirationTime == 4000000000u);
    CHECK(reader->Entries().size() == 3);                                        // k1, k8 and k9
    // DoPeriodicCacheMaintenance compares the same field unsigned, so it keeps an entry whose expiration time has the
    // sign bit set (k9: 4,000,000,000 > now); the getters compare signed, so the very next lookup calls it expired.
    for (int i = 0; i < 41; ++i) reader->GetCachedDataAsData(cRZString("nothing"), nullptr);
    CHECK(reader->Entries().find(cRZString("k9")) != reader->Entries().end());
    CHECK(!reader->IsCachedDataPresent(cRZString("k9")));
    CHECK(reader->Entries().find(cRZString("k9")) == reader->Entries().end());
    delete reader;

    // ---------------------------------------------------------------- SetCacheIniFileName removes the old file
    NetworkFileCache* named = new NetworkFileCache;
    CHECK(named->SetCacheDirectory(cRZString(cacheDirectory.c_str())));
    WriteFile(iniPath, "[Cache Entries]\n");
    CHECK(named->SetCacheIniFileName(cRZString("Other.ini")));
    CHECK(!Exists(iniPath));                                               // the old INI file was removed
    cRZString iniName;
    CHECK(named->GetCacheIniFileName(iniName) && iniName.str() == "Other.ini");
    delete named;

    // ---------------------------------------------------------------- maintenance drops expired entries
    NetworkFileCache* maintenance = new NetworkFileCache;
    CHECK(maintenance->SetCacheDirectory(cRZString(cacheDirectory.c_str())));
    CHECK(maintenance->Init());
    // The 41st access runs DoPeriodicCacheMaintenance, which drops an expired entry (duration 0 = expired at once).
    CHECK(maintenance->AddNewCachedData(cRZString("http://example.com/short"), payload.data(), 4, true, false, 0));
    CHECK(maintenance->Entries().find(cRZString("http://example.com/short")) != maintenance->Entries().end());
    for (int i = 0; i < 39; ++i) maintenance->GetCachedDataAsData(cRZString("http://example.com/nothing"), nullptr);
    CHECK(maintenance->Entries().find(cRZString("http://example.com/short")) != maintenance->Entries().end());
    maintenance->GetCachedDataAsData(cRZString("http://example.com/nothing"), nullptr);
    CHECK(maintenance->Entries().find(cRZString("http://example.com/short")) == maintenance->Entries().end());

    // ---------------------------------------------------------------- teardown
    delete cache;
    delete reloaded;
    delete maintenance;
    std::filesystem::remove_all(root);
    if (g_fail == 0) std::printf("sc4_networkfilecache: all checks passed\n");
    return g_fail == 0 ? 0 : 1;
}
