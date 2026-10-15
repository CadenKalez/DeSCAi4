// cHitIniFile — the audio configuration file (sound .ini) loader (34 functions, 0x1003e1a70..0x1003e4768,
// interface id 0xdd802d53). It derives from cRZIniFile and, on EnumDefaultEntries(), walks the sections the
// sound system is configured by and pushes them into the services:
//   [Config]   DefaultSndDataSourcePlayer2DClassId / ...3DClassId  -> sound manager defaults
//              SndFileDefaultDataSourceClassIds (lines "classId,extension") -> sound system extension table
//              AllowTextAsKey=true -> text keys allowed in directory DB segments
//   [PackedDBSegments]  segmentId = "classId,typeSection,path": a packed .dat opened read-only, registered with
//                       the hit manager and the resource manager; `typeSection` lists type ids whose generic
//                       DB-segment entries are then skipped
//   [TrackDataBlocks]   id = path (relative to the sound data directory) -> hit manager LoadHitFile
//   [HitManTypeGroups]  group = "type,type,..." -> hit manager type-to-group associations
//   [SndDataSourceCreators] section = "classId,id": registers a data source creator under id; the section it
//                       names holds its definitions (key = id, value = definition text)
//   [SndCreators]       id = "defSection,": registers a sound creator; entries of defSection are sound defs
//   [ResourceFactories] type = classId -> persistence resource manager object factories
//   [DBSegments] / [DBSegmentDirectoryFiles]  directory-style DB segments: create, add files, initialise
// Paths starting with "%DataDirectory%" are rewritten to the sound system's data directory. The original
// reaches every service through the framework / GZCOM; here they come from IHitIniHost.
//
// Original layout: +0x00 cRZIniFile, +0x78 interface sub-object (QueryInterface 1 / 0xdd802d53), +0x80 refcount,
// +0x88 std::set<uint> of type ids handled by packed segments, +0xa0 allow-text-as-keys, +0xa8 data directory.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "audio/interfaces/cIGZSndMgr.h"
#include "audio/interfaces/cIGZSndSys.h"
#include "audio/interfaces/cIHitMan.h"
#include "gz/cIGZPersistResourceManager.h"
#include "rz/cRZIniFile.h"

namespace sc4audio {

constexpr std::uint32_t kGZIID_cIHitIniFile = 0xdd802d53;
constexpr const char* kHitDataDirectoryTag = "%DataDirectory%";   // 15 characters

// interface 0x1b6c8b18 of a sound creator: takes sound definitions by id
class cIGZSoundCreatorSoundDefRegistrar : public cIGZUnknown {
public:
    virtual bool AddSoundDef(std::uint32_t id, const cIGZString& definition, std::uint32_t a, std::uint32_t b) = 0;   // [3]
};
constexpr std::uint32_t kGZIID_cIGZSoundCreatorSoundDefRegistrar = 0x1b6c8b18;

// id -> file path table of a directory DB segment (slot 12 of the segment, its slot 3)
class cIHitDirectoryFileMap : public cIGZUnknown {
public:
    virtual bool AddFile(std::uint32_t id, const cIGZString& path) = 0;   // [3]
};

// interface 0x856c8cf7 of the directory-style DB segment classes
class cIHitDirectoryDBSegment : public cIGZUnknown {
public:
    virtual cIGZPersistDBSegment* GetSegment() = 0;                        // [3]
    virtual void Reserved4() {}
    virtual void Reserved5() {}
    virtual void Reserved6() {}
    virtual void Reserved7() {}
    virtual void Reserved8() {}
    virtual void Reserved9() {}
    virtual void Reserved10() {}
    virtual void Reserved11() {}
    virtual cIHitDirectoryFileMap* GetFileMap() = 0;                       // [12]
    virtual void Reserved13() {}
    virtual void Reserved14() {}
    virtual void SetOptions(bool a, bool b) = 0;                           // [15]
    virtual void SetAllowTextAsKeys(bool allow) = 0;                       // [16]
};
constexpr std::uint32_t kGZIID_cIHitDirectoryDBSegment = 0x856c8cf7;

class IHitIniHost {
public:
    virtual ~IHitIniHost() = default;
    virtual cIGZSndMgr* SoundManager() = 0;                        // service 0x1af85bfb
    virtual cIGZSndSys* SoundSystem() = 0;                         // service 0x23cecba0
    virtual cIHitMan* HitManager() = 0;                            // service 0x1b2ce671
    virtual cIGZPersistResourceManager* ResourceManager() = 0;     // service 0x056b906e
    virtual bool CreateInstance(std::uint32_t classId, std::uint32_t iid, void** out) = 0;   // GZCOM
    virtual bool FileExists(const std::string& path) = 0;
    // cGZDBSegmentPackedFile::Init(segmentId, path, flag) on a segment of that class (slot 29)
    virtual bool InitPackedSegment(cIGZPersistDBSegment* segment, std::uint32_t segmentId, const std::string& path, bool flag) = 0;
};

class cHitIniFile : public cRZIniFile {
public:
    explicit cHitIniFile(IHitIniHost* host, cIGZFile* file = nullptr);

    bool SetFilename(const cIGZString& path);
    // Runs every section loader; false when the framework services are missing.
    bool EnumDefaultEntries();

    bool QueryInterface(std::uint32_t iid, void** out);
    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();

    bool InitPackedDBSegments();
    bool InitTrackDataBlocks();
    bool InitHitManTypeGroups();
    bool InitDataSourceCreators();
    bool InitSndCreators();
    bool InitResourceFactories();
    bool InitDBSegments();

    bool ProcessTrackDataBlockEntry(const cRZString& path);
    bool ProcessPackedDBSegmentEntry(std::uint32_t segmentId, const cRZString& value);
    bool ProcessDBSegmentsEntryCreate(std::uint32_t segmentId, const cRZString& value);
    bool ProcessDBSegmentDirectoryFilesEntry(std::uint32_t segmentId, const cRZString& value);
    bool ProcessDBSegmentsEntryInit(std::uint32_t segmentId, const cRZString& value);

    bool AllowTextAsKeys() const { return allowTextAsKeys_; }
    const std::string& DataDirectory() const { return dataDirectory_.str(); }
    const std::set<std::uint32_t>& PackedHandledIds() const { return packedHandled_; }
    // data source creator id -> name of the section holding its definitions
    const std::map<std::uint32_t, cRZString>& DataSourceCreatorSections() const { return dataSourceSections_; }

private:
    using StringMap = std::map<std::uint32_t, cRZString>;
    static void AddLineToStringMap(const cRZString& key, const cRZString& value, void* map);    // IniFileCallbackAddLineToStringMap
    static void DataSourceCreatorsCallback(const cRZString& key, const cRZString& value, void* self);   // IniFileCallbackSndDataSourceCreators
    struct DataSourceEntryContext { cHitIniFile* self; std::uint32_t creatorId; };
    static void DataSourceEntryCallback(const cRZString& key, const cRZString& value, void* context);   // IniFileCallbackSndDataSourceEntry
    static void SoundDefListCallback(const cRZString& key, const cRZString& value, void* creator);      // IniFileCallbackSoundDefList
    StringMap ReadSection(const char* section);
    // "%DataDirectory%rest" -> dataDirectory + rest with platform separators; other paths are kept
    cRZString ResolveDataPath(cRZString path) const;

    IHitIniHost* host_;
    std::uint32_t refCount_ = 0;
    bool allowTextAsKeys_ = false;
    cRZString dataDirectory_;
    std::set<std::uint32_t> packedHandled_;
    StringMap dataSourceSections_;
};

}  // namespace sc4audio
