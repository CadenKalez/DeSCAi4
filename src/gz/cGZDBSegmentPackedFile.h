// cGZDBSegmentPackedFile — a DBPF 1.0 package ("packed file") database segment: read, write,
// compression, hole management. Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (vtable 0x100a9c320, 96 + helper functions); iid 0x286d4a6a.
//
// On-disk format (all little-endian): 0x60-byte header {'DBPF', major=1, minor=0, user version,
// flags, created, modified, index major=7, index count/offset/size, hole count/offset/size, index
// minor, reserved}; records; the index (5 dwords per record: type, group, instance, offset, size);
// the hole table (offset, size pairs); a DIR record (key e86b1eef:e86b1eef:286b1f03) listing the
// uncompressed size of every compressed record as (type, group, instance, size).
// A 16-byte marker 80 9d 88 ec 8f 24 03 6c c9 a6 31 56 5b cf 77 22 may precede the header when the
// package is embedded in another file (FindHeaderRecord scans for it).
//
// Behaviour kept from the original, quirks included:
//  * the in-memory hole table is keyed by size only, so two holes of the same size collapse;
//  * compression is on for every record by default (size 0x32 .. 0xf423e, not the DIR record), and
//    a record is stored compressed only when the result is below 80% of its size;
//  * records above 512 bytes are read lazily from the file, smaller ones are copied to RAM;
//  * a record open for write cannot be opened again (read or write); several readers may share one;
//  * ReallocateSpace is unimplemented (returns false).
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gz/cGZPersistDBSegment.h"
#include "gz/cGZPersistDBSerialRecord.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"

constexpr std::uint32_t kPackedFileHeaderFileIdentifier = 0x46504244;  // 'DBPF'

class cGZDBSegmentPackedFile : public cGZPersistDBSegment {
public:
    enum OpenFlags : std::uint32_t { kOpenRead = 1, kOpenWrite = 2 };
    enum HoleUse : std::uint32_t { kCompactOnFlush = 1, kReuseHoles = 2 };

    cGZDBSegmentPackedFile();

    bool QueryInterface(std::uint32_t iid, void** out) override;

    // Returns 0 (not a database), 1 (plain DBPF) or -1 (error)... see .cpp: mirrors the original's
    // int result: 0 when the file starts with the identifier, -1 otherwise unless `searchEmbedded`.
    static int IsDatabaseFile(const cIGZString& path, bool searchEmbedded);

    // --- cIGZPersistDBSegment ---
    bool Init() override;
    bool Init(std::uint32_t segmentId, const cIGZString* path, bool unused);
    bool Shutdown() override;
    bool Open(bool read, bool write) override;
    bool IsOpen() const override { return (openFlags_ & 3) != 0; }
    bool Close() override;
    bool Flush() override;
    void GetPath(cIGZString& out) const override;
    bool SetPath(const cIGZString& path) override;
    std::uint32_t GetSegmentID() const override { return segmentId_; }
    bool SetSegmentID(const std::uint32_t& id) override { segmentId_ = id; return true; }
    std::uint32_t GetRecordCount(cIGZPersistResourceKeyFilter* filter) override;
    std::uint32_t GetResourceKeyList(cIGZPersistResourceKeyList* list, cIGZPersistResourceKeyFilter* filter) override;
    using cGZPersistDBSegment::GetResourceKeyList;

    // --- configuration / metadata ---
    std::uint32_t GetFileStartingOffset() const { return fileStartingOffset_; }
    bool SetFileStartingOffset(std::uint32_t offset);   // only while closed
    void GetCurrentUserVersion(std::uint32_t& major, std::uint32_t& minor);
    void GetNextUserVersion(std::uint32_t& major, std::uint32_t& minor);
    void SetNextUserVersion(std::uint32_t major, std::uint32_t minor);
    bool GetModificationTime(std::uint32_t& created, std::uint32_t& modified);
    bool SetFileBuffering(std::uint32_t readSize, std::uint32_t writeSize);
    bool SetHoleUseFlags(int flags);
    void SetHoleCompactionMinimumRatio(float ratio) { compactionRatio_ = ratio; }
    bool IsRecordOpen(const cGZPersistResourceKey& key);

    // --- raw access ---
    bool SeekAbsolute(int position);
    bool ReadFileSpan(void* destination, std::uint32_t offset, std::uint32_t byteCount);
    bool ReadFileSpanWithCount(void* destination, std::uint32_t offset, std::uint32_t& byteCount);
    bool WriteFileSpan(const void* source, std::uint32_t offset, std::uint32_t byteCount);
    bool GetRecordAsIGZFile(cIGZPersistDBRecord* record, cIGZFile** file);
    std::uint32_t GetRecordSizeRaw(const cGZPersistResourceKey& key);
    std::uint32_t ReadRecordRaw(const cGZPersistResourceKey& key, void* buffer, std::uint32_t& size);
    bool WriteRecordRaw(const cGZPersistResourceKey& key, const void* buffer, std::uint32_t size);
    std::uint32_t GetRecordPosition(const cGZPersistResourceKey& key);

    // --- statistics / maintenance ---
    std::uint32_t GetUsedTypeCount();
    std::uint32_t GetUsedGroupCount();
    void EnableUsedTypeAndGroupCounting(bool enable);
    bool CompactDatabase();
    bool VerifyDatabase();
    void EnableCompressionForAllRecords(bool enable);
    void EnableCompressionByType(std::uint32_t type, bool enable);
    void EnableCompressionByGroup(std::uint32_t group, bool enable);
    bool GetRecordCompressed(const cGZPersistResourceKey& key);
    void SetRecordCompressed(const cGZPersistResourceKey& key, bool compressed, std::uint32_t uncompressedSize);
    std::uint32_t GetCompressedRecordLength(const cGZPersistResourceKey& key);
    bool AllocateSpace(std::uint32_t& offset, std::uint32_t size);
    bool FreeSpace(std::uint32_t offset, std::uint32_t size);
    bool ReallocateSpace(const cGZPersistResourceKey&, std::uint32_t&, std::uint32_t) { return false; }
    bool GetFreeSpaceMetrics(std::uint32_t& holeCount, std::uint32_t& freeBytes);
    // Copies records between segments (see the .cpp for the direction/keep flags).
    int CopyDatabaseRecords(cIGZPersistDBSegment* other, cIGZPersistResourceKeyFilter* filter, bool intoThis,
                            bool keepSource);
    int CopyDatabaseRecords(const cIGZString& path, cIGZPersistResourceKeyFilter* filter, bool intoThis,
                            bool keepSource);

    struct RecordDataInfo { std::uint32_t offset = 0, size = 0; };
    struct HoleRecord { std::uint32_t offset = 0, size = 0; };
    struct HoleOrder {
        bool operator()(const HoleRecord& a, const HoleRecord& b) const noexcept { return a.size < b.size; }
    };

protected:
    ~cGZDBSegmentPackedFile() override;
    void FinalRelease() override;
    bool DoTestForRecord(const cGZPersistResourceKey& key) override;
    std::uint32_t DoGetRecordSize(const cGZPersistResourceKey& key) override;
    bool DoOpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record, std::uint32_t flags) override;
    bool DoCreateNewRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record) override;
    bool DoCloseRecord(cIGZPersistDBRecord* record) override;
    bool DoAbortRecord(cIGZPersistDBRecord* record) override;
    bool DoDeleteRecord(const cGZPersistResourceKey& key) override;
    std::uint32_t DoReadRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t& size) override;
    bool DoWriteRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t size) override;
    // The path variants stream a record to/from a loose file.
    bool DoReadRecordToFile(const cGZPersistResourceKey& key, const cIGZString& path);
    bool DoWriteRecordFromFile(const cGZPersistResourceKey& key, const cIGZString& path);
    bool DoOpenRecordForWrite(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record);

private:
    friend class cGZDBWriteRecordPackedFile;
    struct Header {
        std::uint32_t identifier = 0, major = 0, minor = 0, userMajor = 0, userMinor = 0, flags = 0;
        std::uint32_t created = 0, modified = 0, indexMajor = 0, indexCount = 0, indexOffset = 0, indexSize = 0;
        std::uint32_t holeCount = 0, holeOffset = 0, holeSize = 0, indexMinor = 0;
        std::uint8_t reserved[32] = {};
    };
    static_assert(sizeof(Header) == 0x60);

    bool WriteEmptyHeaderRecord();
    bool ReadHeaderRecord();
    bool ReadIndexRecord();
    bool ReadHoleRecord();
    bool ReadCompressedSetRecord();
    bool WriteCompressedSetRecord();
    bool WriteIndexRecord();
    bool WriteHoleRecord();
    bool WriteHeaderRecord();
    bool CloseOpenRecords();
    bool ShouldCompactDatabase();
    std::uint32_t FindHeaderRecord();
    bool VerifyHeaderRecordIntegrity();
    bool VerifyIndexRecordIntegrity();
    bool VerifyHoleRecordIntegrity();
    bool WriteRecordInternal(cGZPersistDBSerialRecord* record, const cGZPersistResourceKey& key);
    bool DecompressRecord(std::uint32_t offset, std::uint32_t storedSize, std::vector<std::uint8_t>& out);
    bool DecompressRecord(const cGZPersistResourceKey& key, std::vector<std::uint8_t>& out);
    bool ShouldRecordBeCompressed(const cGZPersistResourceKey& key, std::uint32_t size);
    bool CompressPayload(const std::uint8_t* data, std::uint32_t size, std::vector<std::uint8_t>& out);
    bool DecompressPayload(const std::uint8_t* data, std::uint32_t size, std::vector<std::uint8_t>& out);
    void IncrementTypeAndGroupUse(std::uint32_t type, std::uint32_t group, bool increment);
    bool CopyDatabaseRecord(cIGZPersistDBSegment* other, const cGZPersistResourceKey& key, bool intoThis,
                            bool keepSource);
    void RemoveIndexedRecord(const cGZPersistResourceKey& key, std::map<cGZPersistResourceKey, RecordDataInfo, cGZPersistResourceKeyOrder>::iterator it);

    bool initialized_ = false;
    std::uint32_t segmentId_ = 0;
    std::uint32_t openFlags_ = 0;
    std::map<cGZPersistResourceKey, RecordDataInfo, cGZPersistResourceKeyOrder> index_;
    bool indexDirty_ = false;
    cRZString path_;
    cRZFile file_;
    std::uint32_t fileStartingOffset_ = 0;
    std::uint32_t readBuffering_ = 0xffffffffu, writeBuffering_ = 0xffffffffu;
    std::uint32_t nextUserMajor_ = 0, nextUserMinor_ = 0;
    std::uint32_t lazyReadThreshold_ = 0x200;
    std::multimap<cGZPersistResourceKey, cGZPersistDBSerialRecord*, cGZPersistResourceKeyOrder> openRecords_;
    Header header_;
    std::uint32_t holeUseFlags_ = 3;
    float compactionRatio_ = 0.15f;
    std::set<HoleRecord, HoleOrder> holes_;
    bool holesDirty_ = false;
    bool compacting_ = false;
    bool countTypesAndGroups_ = false;
    std::unordered_map<std::uint32_t, std::int32_t> typeUse_, groupUse_;
    bool compressedSetDirty_ = false;
    bool compressAll_ = true;
    std::unordered_set<std::uint32_t> compressTypes_, compressGroups_;
    std::map<cGZPersistResourceKey, std::uint32_t, cGZPersistResourceKeyOrder> compressed_;  // key -> uncompressed size
};
