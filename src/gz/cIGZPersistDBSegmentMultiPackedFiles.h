// cIGZPersistDBSegmentMultiPackedFiles — a segment that unions every package in a directory tree.
// Slots 3..10 from the interface sub-vtable of cGZPersistDBSegmentMultiPackedFiles (0x100ab9be0);
// iid 0xe90f85f3.
#pragma once
#include <cstdint>

#include "gz/cIGZPersistDBSegment.h"

class cIGZPersistDBSegmentMultiPackedFiles : public cIGZUnknown {
public:
    virtual void SetPathFilter(const cIGZString& pattern) = 0;                                  // [3]
    virtual int ConsolidateDatabaseRecords(cIGZPersistDBSegment* destination,
                                           cIGZPersistResourceKeyFilter* filter) = 0;           // [4]
    virtual std::uint32_t ConsolidateDatabaseRecords(const cIGZString& path,
                                                     cIGZPersistResourceKeyFilter* filter) = 0; // [5]
    virtual bool FindDBSegment(const cGZPersistResourceKey& key, cIGZPersistDBSegment** segment) = 0;  // [6] AddRef'd
    virtual std::uint32_t GetSegmentCount() = 0;                                                // [7]
    virtual cIGZPersistDBSegment* GetSegmentByIndex(std::uint32_t index) = 0;                   // [8]
    virtual void AddedResource(const cGZPersistResourceKey& key, cIGZPersistDBSegment* segment) = 0;   // [9]
    virtual void RemovedResource(const cGZPersistResourceKey& key, cIGZPersistDBSegment* segment) = 0; // [10]
};
constexpr std::uint32_t kGZIID_cIGZPersistDBSegmentMultiPackedFiles = 0xe90f85f3;
