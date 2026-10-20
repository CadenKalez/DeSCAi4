// cGZDBSegmentDirectoryFiles — a database segment whose records are loose files in one directory (the "SDF" segment;
// iid 0x856c8cf7 for the directory interface). Recovered from Sim City 4 Deluxe Edition 1.2.1: the class lives at
// 0x1004d3a90..0x1004d7d42 (+ 0x1004d89d0..0x1004d8a4a for the inline accessors); the record object it hands out is
// cGZDBSDFRecord (gz/cGZDBSDFRecord.h, already ported), the helper map of this file is cGZRESIDtoExtensionMap
// (iid 0xa56c8cfd) and cGZFileNameToResKeyConverterDefault (0x1004d77e4..0x1004d7cde) is the unused default converter.
//
// Original layout (primary vtable 0x100ab7d90, the cGZPersistDBSegment base sits at +8 with vtable 0x100ab7f40):
//   +0x28 initialized, +0x29 "writing needs an existing directory" (1 after Init), +0x2a (1 in the ctor, 0 after Init;
//   never read), +0x2c next instance id for GenerateNewKey (starts at 1), +0x30 open flags (bit0 read, bit1 write),
//   +0x38 cRZString directory (always ends in '/'), +0x60 cRZDirectory*, +0x68 default group id, +0x6c segment id,
//   +0x70 allow "instance[group]" prefix, +0x71 allow "[instance]group" suffix, +0x72 allow text file names (CRC24),
//   +0x78 cGZRESIDtoExtensionMap, +0xa0 map<key, RecordData> (RecordData = file name + list of open records),
//   +0xb8 file change detection on, +0xbc detection interval in seconds (default 0x78), +0xc0 cRZCallbackTimer,
//   +0xf8 cRZRandom (seed 0xffffffff).
//
// File names: "<instance:8 hex>.<ext>" for the default group, "<instance:8><group:8>.<ext>" otherwise; the extension
// comes from the RESID (type) <-> extension map, which the registry category "CN_DBSDF" fills (see
// CreateDBSegmentDirectoryFilesCallback: "<type hex>:<ext>[:filenameasinstanceid], ..." for the segment whose id equals
// the category entry's key).
//
// Quirks kept:
//  * Init(id, path, bool): the third argument is ignored; a failing registry registration returns false and leaves the
//    segment initialized; a failing SetPath/Open shuts it down.
//  * Open() leaves the open flags set when the registry registration or BuildKeyList fails.
//  * the extension map is a multimap (AddRESIDtoExtensionAssociation never rejects duplicates, lookups find the first).
//  * AddFile(name, true) always returns false (even when the file was added); AddFile(name, false) returns true when a
//    record without open handles was removed.
//  * DoFileChangeCheck does nothing when the segment has no records or the directory has no valid files.
//  * DoWriteRecord is not implemented (returns false); Flush() always returns true.
//  * map ordering is (type, group, instance), the std::less of cGZPersistResourceKey in the binary.
//  * the base's reference count is not atomic here (the original uses an interlocked add on the same field).
// Not recovered / behind seams: the framework registry (DirectoryFilesServices::registry), file system and language
// manager services; the timer's SDL thread is cRZCallbackTimer.
#pragma once
#include <cstdint>
#include <functional>
#include <list>
#include <map>

#include "gz/cGZDBSDFRecord.h"
#include "gz/cGZPersistDBSegment.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZCallbackTimer.h"
#include "rz/cRZDirectory.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"

constexpr std::uint32_t kGZIID_cIGZPersistDBSegmentDirectoryFiles = 0x856c8cf7;
constexpr std::uint32_t kGZIID_cIGZPersistRESIDtoExtensionMap = 0xa56c8cfd;
constexpr char kGZCATNAME_cGZPersistDBSegmentDirectoryFiles[] = "CN_DBSDF";   // string at 0x1009f562f

// cIGZPersistRESIDtoExtensionMap — vtable 0x100ab8670.
class cIGZPersistRESIDtoExtensionMap : public cIGZUnknown {
public:
    virtual bool AddRESIDtoExtensionAssociation(std::uint32_t resid, const cIGZString& extension) = 0;   // [3]
    virtual bool ExtensionToRESID(const cIGZString& extension, std::uint32_t& resid) = 0;                // [4]
    virtual bool RESIDToExtension(const std::uint32_t& resid, cIGZString& extension) = 0;                // [5]
    virtual void EraseAll() = 0;                                                                         // [6]
    virtual void Copy(const cIGZPersistRESIDtoExtensionMap* other) = 0;                                  // [7]
    virtual std::uint32_t Size() = 0;                                                                    // [10]
    virtual ~cIGZPersistRESIDtoExtensionMap() = default;                                                 // [8],[9]
};

class cGZRESIDtoExtensionMap final : public cIGZPersistRESIDtoExtensionMap {
public:
    explicit cGZRESIDtoExtensionMap(std::uint32_t initialRefCount = 1) : refCount_(initialRefCount) {}
    ~cGZRESIDtoExtensionMap() override = default;                                                    // @ 0x1004d3c1c / 0x1004d89d0

    bool QueryInterface(std::uint32_t iid, void** out) override;                                     // @ 0x1004d7cf6 (1, 0xa56c8cfd)
    std::uint32_t AddRef() override { return ++refCount_; }                                          // @ 0x1004d7d18
    std::uint32_t Release() override;                                                                // @ 0x1004d7d26
    bool AddRESIDtoExtensionAssociation(std::uint32_t resid, const cIGZString& extension) override;  // @ 0x1004d5e16
    bool ExtensionToRESID(const cIGZString& extension, std::uint32_t& resid) override;               // @ 0x1004d5bc8
    bool RESIDToExtension(const std::uint32_t& resid, cIGZString& extension) override;               // @ 0x1004d56c2
    void EraseAll() override { entries_.clear(); }                                                   // @ 0x1004d49d0
    void Copy(const cIGZPersistRESIDtoExtensionMap* other) override;                                 // @ 0x1004d7d42
    std::uint32_t Size() override { return static_cast<std::uint32_t>(entries_.size()); }            // @ 0x1004d7dbc

private:
    std::uint32_t refCount_;                         // +8
    std::multimap<std::uint32_t, cRZString> entries_;   // +0x10
};

// cIGZPersistDBSegmentDirectoryFiles — the directory interface (primary vtable slots 3..25).
class cIGZPersistDBSegmentDirectoryFiles : public cIGZUnknown {
public:
    virtual cIGZPersistDBSegment* AsIDBSegment() = 0;                                            // [3] +0x18
    virtual bool Init() = 0;                                                                     // [4]
    virtual bool Shutdown() = 0;                                                                 // [5]
    virtual bool Init(std::uint32_t segmentId, const cIGZString* path, bool unused) = 0;         // [6]
    virtual const cRZString& Path() const = 0;                                                   // [7]
    virtual void GetPath(cIGZString& out) const = 0;                                             // [8]
    virtual bool SetPath(const cIGZString& path) = 0;                                            // [9]
    virtual void SetDefaultGroupId(std::uint32_t group) = 0;                                     // [10]
    virtual std::uint32_t GroupId() const = 0;                                                   // [11]
    virtual cGZRESIDtoExtensionMap* ExtensionMap() = 0;                                          // [12]
    virtual bool GetRESIDtoExtensionMap(cIGZPersistRESIDtoExtensionMap** out) = 0;               // [13]
    virtual void AddExtensionAssociation(std::uint32_t resid, const cIGZString& extension) = 0;  // [14]
    virtual void SetAllowedIDPositions(bool prefix, bool suffix) = 0;                            // [15]
    virtual void SetAllowTextAsKey(bool allow) = 0;                                              // [16]
    virtual bool GetFileNameFromKey(const cGZPersistResourceKey& key, cIGZString& out) = 0;      // [17]
    virtual bool GetFilePathFromKey(const cGZPersistResourceKey& key, cIGZString& out) = 0;      // [18]
    virtual bool GetKeyFromFileName(const cIGZString& name, const cIGZString& extension,
                                    cGZPersistResourceKey& key) = 0;                             // [19]
    virtual bool IsFileNameValid(const cIGZString& fileName) = 0;                                // [20]
    virtual bool GenerateNewKey(std::uint32_t type, cGZPersistResourceKey& key) = 0;             // [21]
    virtual std::uint32_t EnumerateRecordFiles(cIGZString** names, std::uint32_t count) = 0;     // [22]
    virtual bool GetFileChangeDetection() = 0;                                                   // [23]
    virtual bool SetFileChangeDetection(bool enable, std::uint32_t intervalSeconds) = 0;         // [24]
    virtual bool DoFileChangeCheck() = 0;                                                        // [25]
};

// cGZFileNameToResKeyConverterDefault — vtable 0x100ab80a0. Never wired to anything in the binary; FileNameToKey
// parses the name and throws the result away, always answering false (so IsFileNameValid is always false too).
class cGZFileNameToResKeyConverterDefault {
public:
    cGZFileNameToResKeyConverterDefault() = default;                              // @ 0x1004d77e4
    virtual ~cGZFileNameToResKeyConverterDefault() = default;                     // @ 0x1004d780a / 0x1004d7810 / 0x1004d7816
    virtual bool IsFileNameValid(const cIGZString& fileName);                     // @ 0x1004d781c
    virtual bool FileNameToKey(const cIGZString& fileName, cGZPersistResourceKey& key);   // @ 0x1004d7842
    virtual bool KeyToFileName(const cGZPersistResourceKey&, cIGZString&) { return false; }   // @ 0x1004d7cd6
    virtual void SetDBSegment(cIGZPersistDBSegmentDirectoryFiles* segment) { segment_ = segment; }   // @ 0x1004d7cde

private:
    cIGZPersistDBSegmentDirectoryFiles* segment_ = nullptr;   // +8
    bool allowPrefix_ = true;                                  // +0x10
    bool allowSuffix_ = false;                                 // +0x11 (the ctor stores 0x0001 at +0x10)
    std::uint32_t unknown14_ = 0;                              // +0x14
};

// The framework registry service (service 0x25076b9e, iid 0xa237613c) as far as the segment uses it: slot 7 registers a
// callback for a category of the registry (the callback is fed every key/value entry of that category).
using DirectoryFilesCategoryCallback = void (*)(const cIGZString& key, const cIGZString& value, void* user);
class cIGZRegistryCategories {
public:
    virtual bool RegisterCategoryCallback(const char* category, DirectoryFilesCategoryCallback callback, void* user) = 0;
protected:
    ~cIGZRegistryCategories() = default;
};

// The framework services Init / Open reach through RZGetFramework(); any may be null (then that step is skipped, as in
// the binary when the framework does not offer the service).
struct DirectoryFilesServices {
    cIGZRegistryCategories* registry = nullptr;
    // cIGZFileSystem::DataDirectory() (service 0x2073215d), used for "%DataDirectory%"; unset = no file system service.
    std::function<const char*()> dataDirectory;
    // cIGZLanguageManager::GetLanguageDirectoryName(out, 0) (service 0x441e5070), used for "%Language%"; unset = none.
    std::function<void(cRZString& out)> languageDirectory;
};
void SetDirectoryFilesServices(const DirectoryFilesServices& services);
const DirectoryFilesServices& GetDirectoryFilesServices();

class cGZDBSegmentDirectoryFiles final : public cIGZPersistDBSegmentDirectoryFiles, public cGZPersistDBSegment {
public:
    struct RecordData {
        cRZString fileName;                                          // +0x30 in the tree node
        std::list<cRZAutoRefCount<cGZDBSDFRecord>> records;          // +0x58
        void EraseRecord(cIGZPersistDBRecord* record);               // @ 0x1004d5016
    };
    struct KeyLess {
        bool operator()(const cGZPersistResourceKey& a, const cGZPersistResourceKey& b) const noexcept {
            if (a.type != b.type) return a.type < b.type;
            if (a.group != b.group) return a.group < b.group;
            return a.instance < b.instance;
        }
    };
    using RecordMap = std::map<cGZPersistResourceKey, RecordData, KeyLess>;

    cGZDBSegmentDirectoryFiles();                                    // @ 0x1004d3a90 / 0x1004d3c38

    // cIGZUnknown (both views)
    bool QueryInterface(std::uint32_t iid, void** out) override;     // @ 0x1004d3d96 / 0x1004d3db8
    std::uint32_t AddRef() override { return cGZPersistDBSegment::AddRef(); }     // @ 0x1004d3dde / 0x1004d3df0
    std::uint32_t Release() override { return cGZPersistDBSegment::Release(); }   // @ 0x1004d3e02 / 0x1004d3e22

    // cIGZPersistDBSegmentDirectoryFiles
    cIGZPersistDBSegment* AsIDBSegment() override { return static_cast<cIGZPersistDBSegment*>(this); }   // @ 0x1004d560c
    bool Init() override;                                            // @ 0x1004d3ece / 0x1004d3ee0
    bool Shutdown() override;                                        // @ 0x1004d48cc / 0x1004d49fe
    bool Init(std::uint32_t segmentId, const cIGZString* path, bool unused) override;   // @ 0x1004d3ef2 / 0x1004d48bc
    const cRZString& Path() const override { return path_; }         // @ 0x1004d89fe
    void GetPath(cIGZString& out) const override;                    // @ 0x1004d4d6e / 0x1004d4da2
    bool SetPath(const cIGZString& path) override;                   // @ 0x1004d4dda / 0x1004d4e3c
    void SetDefaultGroupId(std::uint32_t group) override { groupId_ = group; }   // @ 0x1004d8a08
    std::uint32_t GroupId() const override { return groupId_; }      // @ 0x1004d8a12
    cGZRESIDtoExtensionMap* ExtensionMap() override { return &extensionMap_; }   // @ 0x1004d8a1c
    bool GetRESIDtoExtensionMap(cIGZPersistRESIDtoExtensionMap** out) override;   // @ 0x1004d55b0
    void AddExtensionAssociation(std::uint32_t resid, const cIGZString& extension) override {   // @ 0x1004d5e08
        extensionMap_.AddRESIDtoExtensionAssociation(resid, extension);
    }
    void SetAllowedIDPositions(bool prefix, bool suffix) override { allowPrefix_ = prefix; allowSuffix_ = suffix; }   // @ 0x1004d5812
    void SetAllowTextAsKey(bool allow) override { allowText_ = allow; }   // @ 0x1004d5820
    bool GetFileNameFromKey(const cGZPersistResourceKey& key, cIGZString& out) override;   // @ 0x1004d5616
    bool GetFilePathFromKey(const cGZPersistResourceKey& key, cIGZString& out) override;   // @ 0x1004d5754
    bool GetKeyFromFileName(const cIGZString& name, const cIGZString& extension, cGZPersistResourceKey& key) override;   // @ 0x1004d582a
    bool IsFileNameValid(const cIGZString& fileName) override;       // @ 0x1004d5d00
    bool GenerateNewKey(std::uint32_t type, cGZPersistResourceKey& key) override;   // @ 0x1004d65d8
    std::uint32_t EnumerateRecordFiles(cIGZString** names, std::uint32_t count) override;   // @ 0x1004d6640
    bool GetFileChangeDetection() override { return changeDetection_; }   // @ 0x1004d72bc
    bool SetFileChangeDetection(bool enable, std::uint32_t intervalSeconds) override;   // @ 0x1004d72c8
    bool DoFileChangeCheck() override;                               // @ 0x1004d7332

    // cIGZPersistDBSegment
    bool Open(bool read, bool write) override;                       // @ 0x1004d4a10 / 0x1004d4be6
    bool IsOpen() const override { return (openFlags_ & 3) != 0; }   // @ 0x1004d8a26 / 0x1004d8a3c
    bool Close() override;                                           // @ 0x1004d4bfc / 0x1004d4d5c
    bool Flush() override { return true; }                           // @ 0x1004d8a34 / 0x1004d8a4a
    std::uint32_t GetSegmentID() const override { return segmentId_; }   // @ 0x1004d3e8a / 0x1004d3e94
    bool SetSegmentID(const std::uint32_t& id) override;             // @ 0x1004d3e9e / 0x1004d3eb6
    std::uint32_t GetRecordCount(cIGZPersistResourceKeyFilter* filter) override;   // @ 0x1004d4e4a / 0x1004d4e64
    std::uint32_t GetResourceKeyList(cIGZPersistResourceKeyList* list, cIGZPersistResourceKeyFilter* filter) override;   // @ 0x1004d4e82 / 0x1004d4f2c
    using cGZPersistDBSegment::GetResourceKeyList;

    // derived virtuals of the primary vtable (+0x178..+0x198)
    virtual bool OpenRecordInternal(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record,
                                    cIGZPersistDBSDFRecord::dbsdfFlags mode);   // @ 0x1004d6088
    virtual bool BuildKeyList();                                     // @ 0x1004d674e
    virtual bool AddFile(const cRZString& fileName, bool add);       // @ 0x1004d6d3a
    virtual bool getFileNameFromKey(const cGZPersistResourceKey& key, cRZString& name, cRZString& extension);   // @ 0x1004d565a
    virtual bool createFileNameFromKey(const cGZPersistResourceKey& key, cRZString& name, cRZString& extension);   // @ 0x1004d5742

    static void StaticBackgroundThreadFunction(void* segment);       // @ 0x1004d77d6
    // The registry callback (CreateDBSegmentDirectoryFilesCallback @ 0x1004d4566).
    static void CreateDBSegmentDirectoryFilesCallback(const cIGZString& key, const cIGZString& value, void* segment);

protected:
    ~cGZDBSegmentDirectoryFiles() override;                          // @ 0x1004d3c42 / 0x1004d3d5a (+ thunks 0x1004d3d4c, 0x1004d3d88)
    void FinalRelease() override;                                    // @ 0x1004d3e46 / 0x1004d3e68
    bool DoTestForRecord(const cGZPersistResourceKey& key) override;           // @ 0x1004d5104 / 0x1004d512e
    std::uint32_t DoGetRecordSize(const cGZPersistResourceKey& key) override;  // @ 0x1004d5296 / 0x1004d53b8
    bool DoOpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record, std::uint32_t flags) override;   // @ 0x1004d4f3a / 0x1004d4f70
    bool DoCreateNewRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record) override;   // @ 0x1004d516e / 0x1004d5184
    bool DoCloseRecord(cIGZPersistDBRecord* record) override;        // @ 0x1004d4f7e / 0x1004d504c
    bool DoAbortRecord(cIGZPersistDBRecord* record) override;        // @ 0x1004d505a / 0x1004d50f6
    bool DoDeleteRecord(const cGZPersistResourceKey& key) override;  // @ 0x1004d51a0 / 0x1004d5288
    std::uint32_t DoReadRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t& size) override;   // @ 0x1004d53c6 / 0x1004d5592
    bool DoWriteRecord(const cGZPersistResourceKey&, void*, std::uint32_t) override { return false; }   // @ 0x1004d55a0 / 0x1004d55a8
    virtual bool DoOpenRecordForWrite(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record);   // @ 0x1004d5158

private:
    void CloseOpenRecords();                                         // @ 0x1004d4cc4
    void StartBackgroundThread();                                    // @ 0x1004d4b82
    RecordMap::iterator FindRecord(const cGZPersistResourceKey& key) { return records_.find(key); }
    cRZString RecordPath(const RecordData& data) const;

    bool initialized_ = false;                   // +0x28
    bool requireDirectoryForWrite_ = false;      // +0x29
    bool flag2a_ = true;                         // +0x2a
    std::uint32_t nextInstance_ = 1;             // +0x2c
    std::uint32_t openFlags_ = 0;                // +0x30
    cRZString path_;                             // +0x38
    cRZDirectory* directory_ = nullptr;          // +0x60
    std::uint32_t groupId_ = 0;                  // +0x68
    std::uint32_t segmentId_ = 0;                // +0x6c
    bool allowPrefix_ = true;                    // +0x70
    bool allowSuffix_ = false;                   // +0x71 (ctor writes 0x0001 to +0x70/+0x71)
    bool allowText_ = false;                     // +0x72
    cGZRESIDtoExtensionMap extensionMap_{0};     // +0x78
    RecordMap records_;                          // +0xa0
    bool changeDetection_ = false;               // +0xb8
    std::uint32_t detectionInterval_ = 0x78;     // +0xbc
    cRZCallbackTimer timer_;                     // +0xc0
    cRZRandom random_{0xffffffffu};              // +0xf8
};
