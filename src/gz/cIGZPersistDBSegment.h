// cIGZPersistDBSegment — a database segment of the persistence layer (a DBPF file, or a directory
// of loose files). Slots 3..28 recovered from the interface sub-vtable of cGZDBSegmentPackedFile
// (0x100a9c550); iid 0x656b8f0c.
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZPersistRecord.h"
#include "gz/cIGZPersistResourceKeyList.h"
#include "gz/cIGZString.h"

class cIGZPersistDBSegment : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                            // [3]
    virtual bool Shutdown() = 0;                                        // [4]
    virtual bool Open(bool read, bool write) = 0;                       // [5]
    virtual bool IsOpen() const = 0;                                    // [6]
    virtual bool Close() = 0;                                           // [7]
    virtual bool Flush() = 0;                                           // [8]
    virtual void GetPath(cIGZString& out) const = 0;                    // [9]
    virtual bool SetPath(const cIGZString& path) = 0;                   // [10]
    virtual bool Lock() = 0;                                            // [11]
    virtual bool Unlock() = 0;                                          // [12]
    virtual std::uint32_t GetSegmentID() const = 0;                     // [13]
    virtual bool SetSegmentID(const std::uint32_t& id) = 0;             // [14]
    virtual std::uint32_t GetRecordCount(cIGZPersistResourceKeyFilter* filter) = 0;  // [15]
    virtual std::uint32_t GetResourceKeyList(cIGZPersistResourceKeyList* list,
                                             cIGZPersistResourceKeyFilter* filter) = 0;  // [16]
    virtual bool GetResourceKeyList(cIGZPersistResourceKeyList& list) = 0;               // [17]
    virtual bool TestForRecord(const cGZPersistResourceKey& key) = 0;                    // [18]
    virtual std::uint32_t GetRecordSize(const cGZPersistResourceKey& key) = 0;           // [19] 0xffffffff if absent
    // [20] flags: bit0 read, bit1 write.
    virtual bool OpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record,
                            std::uint32_t flags) = 0;
    virtual bool CreateNewRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record) = 0;  // [21]
    virtual bool CloseRecord(cIGZPersistDBRecord* record) = 0;                           // [22]
    virtual bool CloseRecord(cIGZPersistDBRecord** record) = 0;                          // [23] releases too
    virtual bool AbortRecord(cIGZPersistDBRecord* record) = 0;                           // [24]
    virtual bool AbortRecord(cIGZPersistDBRecord** record) = 0;                          // [25]
    virtual bool DeleteRecord(const cGZPersistResourceKey& key) = 0;                     // [26]
    // [27] `size` is the buffer capacity on entry (null buffer: only report the size) and the number
    // of bytes read on exit. Returns the size or 0xffffffff.
    virtual std::uint32_t ReadRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t& size) = 0;
    virtual bool WriteRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t size) = 0;  // [28]
};
constexpr std::uint32_t kGZIID_cIGZPersistDBSegment = 0x656b8f0c;
constexpr std::uint32_t kGZIID_cGZDBSegmentPackedFile = 0x286d4a6a;

// A segment that does nothing: every query fails. Base for test doubles that only need the
// cIGZUnknown part.
class cIGZPersistDBSegmentStub : public cIGZPersistDBSegment {
public:
    bool Init() override { return false; }
    bool Shutdown() override { return false; }
    bool Open(bool, bool) override { return false; }
    bool IsOpen() const override { return false; }
    bool Close() override { return false; }
    bool Flush() override { return false; }
    void GetPath(cIGZString&) const override {}
    bool SetPath(const cIGZString&) override { return false; }
    bool Lock() override { return false; }
    bool Unlock() override { return false; }
    std::uint32_t GetSegmentID() const override { return 0; }
    bool SetSegmentID(const std::uint32_t&) override { return false; }
    std::uint32_t GetRecordCount(cIGZPersistResourceKeyFilter*) override { return 0; }
    std::uint32_t GetResourceKeyList(cIGZPersistResourceKeyList*, cIGZPersistResourceKeyFilter*) override { return 0; }
    bool GetResourceKeyList(cIGZPersistResourceKeyList&) override { return false; }
    bool TestForRecord(const cGZPersistResourceKey&) override { return false; }
    std::uint32_t GetRecordSize(const cGZPersistResourceKey&) override { return 0xffffffffu; }
    bool OpenRecord(const cGZPersistResourceKey&, cIGZPersistDBRecord**, std::uint32_t) override { return false; }
    bool CreateNewRecord(const cGZPersistResourceKey&, cIGZPersistDBRecord**) override { return false; }
    bool CloseRecord(cIGZPersistDBRecord*) override { return false; }
    bool CloseRecord(cIGZPersistDBRecord**) override { return false; }
    bool AbortRecord(cIGZPersistDBRecord*) override { return false; }
    bool AbortRecord(cIGZPersistDBRecord**) override { return false; }
    bool DeleteRecord(const cGZPersistResourceKey&) override { return false; }
    std::uint32_t ReadRecord(const cGZPersistResourceKey&, void*, std::uint32_t&) override { return 0xffffffffu; }
    bool WriteRecord(const cGZPersistResourceKey&, void*, std::uint32_t) override { return false; }
};
