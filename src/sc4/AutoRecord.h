// cSC4AutoLoadRecord / cSC4AutoSaveRecord — RAII "sub-record" helpers a serializable uses inside
// its Read/Write to attach an extra record (key {type, group, instance}) of the segment its stream
// belongs to. The record starts with a u16 version: load reads it into GetVersion(), save writes
// the version passed to the constructor. Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (cSC4AutoLoadRecord 0x100042634.., cSC4AutoSaveRecord 0x1000428ea..).
//
// Constructed from a stream, the helper resolves the stream's database segment (the stream must be
// a cISC4DBSegmentIStream/OStream view) and silently stays closed otherwise. Close() is also run by
// the destructor.
#pragma once
#include <cstdint>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZPersistDBSegment.h"
#include "gz/cIGZPersistDBSerialRecord.h"
#include "rz/cRZAutoRefCount.h"

namespace sc4 {

class cSC4AutoLoadRecord {
public:
    cSC4AutoLoadRecord(cIGZPersistDBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance);
    cSC4AutoLoadRecord(cIGZIStream& stream, std::uint32_t type, std::uint32_t group, std::uint32_t instance);
    ~cSC4AutoLoadRecord() { Close(); }
    cSC4AutoLoadRecord(const cSC4AutoLoadRecord&) = delete;
    cSC4AutoLoadRecord& operator=(const cSC4AutoLoadRecord&) = delete;

    void Close();
    bool IsOpen() const { return record_.get() != nullptr; }
    cIGZPersistDBSerialRecord* GetRecord() const { return record_.get(); }
    std::uint16_t GetVersion() const { return version_; }

private:
    bool OpenRecord(cIGZPersistDBSegment* segment, const cGZPersistResourceKey& key);
    cRZAutoRefCount<cIGZPersistDBSegment> segment_;        // +8
    cRZAutoRefCount<cIGZPersistDBSerialRecord> record_;    // +0x10
    std::uint16_t version_ = 0xffff;                       // +0x18
};

class cSC4AutoSaveRecord {
public:
    cSC4AutoSaveRecord(cIGZPersistDBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance,
                       std::uint16_t version);
    cSC4AutoSaveRecord(cIGZOStream& stream, std::uint32_t type, std::uint32_t group, std::uint32_t instance,
                       std::uint16_t version);
    ~cSC4AutoSaveRecord() { Close(); }
    cSC4AutoSaveRecord(const cSC4AutoSaveRecord&) = delete;
    cSC4AutoSaveRecord& operator=(const cSC4AutoSaveRecord&) = delete;

    void Close();
    bool IsOpen() const { return record_.get() != nullptr; }
    cIGZPersistDBSerialRecord* GetRecord() const { return record_.get(); }

private:
    bool OpenRecord(cIGZPersistDBSegment* segment, const cGZPersistResourceKey& key, std::uint16_t version);
    cRZAutoRefCount<cIGZPersistDBSegment> segment_;
    cRZAutoRefCount<cIGZPersistDBSerialRecord> record_;
    std::uint16_t version_ = 0xffff;
};

}  // namespace sc4
