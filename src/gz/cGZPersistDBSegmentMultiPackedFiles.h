// cGZPersistDBSegmentMultiPackedFiles — mounts every "*.dat" under a directory (read-only) and
// answers record queries from whichever package supplied the key last. Recovered from
// Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100ab9a40, 38 functions).
//
// Deviations: the directory walk is sorted case-insensitively by full path (the original uses the
// OS enumeration order of cRZDirectory); packages are created directly instead of through the COM
// director (GZCOM class ids 0x086d4a89 / 0x656b8f2d).
// Quirks kept: opening for write (or without read) fails; create/delete/write are refused;
// GetRecordCount returns the number of distinct keys across all packages.
#pragma once
#include <cstdint>
#include <map>
#include <vector>

#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistDBSegment.h"
#include "gz/cIGZPersistDBSegmentMultiPackedFiles.h"

class cGZPersistDBSegmentMultiPackedFiles : public cGZPersistDBSegment,
                                            public cIGZPersistDBSegmentMultiPackedFiles {
public:
    cGZPersistDBSegmentMultiPackedFiles();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZPersistDBSegment::AddRef(); }
    std::uint32_t Release() override { return cGZPersistDBSegment::Release(); }

    bool Init() override;
    bool Init(std::uint32_t segmentId, const cIGZString* path, bool unused);
    bool Shutdown() override;
    bool Open(bool read, bool write) override;
    bool IsOpen() const override { return open_; }
    bool Close() override;
    bool Flush() override { return true; }
    void GetPath(cIGZString& out) const override { out.AssignFromChars(rootPath_.Data(), rootPath_.Strlen()); }
    bool SetPath(const cIGZString& path) override;
    std::uint32_t GetSegmentID() const override { return segmentId_; }
    bool SetSegmentID(const std::uint32_t& id) override { segmentId_ = id; return true; }
    std::uint32_t GetRecordCount(cIGZPersistResourceKeyFilter*) override {
        return static_cast<std::uint32_t>(resources_.size());
    }
    std::uint32_t GetResourceKeyList(cIGZPersistResourceKeyList* list, cIGZPersistResourceKeyFilter* filter) override;
    using cGZPersistDBSegment::GetResourceKeyList;

    // cIGZPersistDBSegmentMultiPackedFiles
    void SetPathFilter(const cIGZString& pattern) override { filter_.AssignFromChars(pattern.Data(), pattern.Strlen()); }
    int ConsolidateDatabaseRecords(cIGZPersistDBSegment* destination, cIGZPersistResourceKeyFilter* filter) override;
    std::uint32_t ConsolidateDatabaseRecords(const cIGZString& path, cIGZPersistResourceKeyFilter* filter) override;
    bool FindDBSegment(const cGZPersistResourceKey& key, cIGZPersistDBSegment** segment) override;
    std::uint32_t GetSegmentCount() override { return static_cast<std::uint32_t>(segments_.size()); }
    cIGZPersistDBSegment* GetSegmentByIndex(std::uint32_t index) override { return segments_[index]; }
    void AddedResource(const cGZPersistResourceKey& key, cIGZPersistDBSegment* segment) override;
    void RemovedResource(const cGZPersistResourceKey& key, cIGZPersistDBSegment*) override { resources_.erase(key); }

protected:
    ~cGZPersistDBSegmentMultiPackedFiles() override = default;
    bool DoTestForRecord(const cGZPersistResourceKey& key) override;
    std::uint32_t DoGetRecordSize(const cGZPersistResourceKey& key) override;
    bool DoOpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record, std::uint32_t flags) override;
    bool DoCreateNewRecord(const cGZPersistResourceKey&, cIGZPersistDBRecord**) override { return false; }
    bool DoCloseRecord(cIGZPersistDBRecord* record) override;
    bool DoAbortRecord(cIGZPersistDBRecord* record) override;
    bool DoDeleteRecord(const cGZPersistResourceKey&) override { return false; }
    std::uint32_t DoReadRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t& size) override;
    bool DoWriteRecord(const cGZPersistResourceKey&, void*, std::uint32_t) override { return false; }

private:
    cIGZPersistDBSegment* SegmentFor(const cGZPersistResourceKey& key) const;

    bool initialized_ = false;
    std::uint32_t segmentId_ = 0;
    cRZString rootPath_;
    cRZString filter_{"*.dat"};
    bool open_ = false;
    std::vector<cIGZPersistDBSegment*> segments_;   // owned references
    std::map<cGZPersistResourceKey, cIGZPersistDBSegment*, cGZPersistResourceKeyOrder> resources_;
};
