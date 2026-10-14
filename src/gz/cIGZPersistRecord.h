// Persistence-layer interfaces the audio data sources use to read resources out of packed
// databases. The persistence layer (cGZPersistResourceManager, cGZPersistDBRecord, ...) is not
// recovered yet, so only the calls the audio code makes are declared. Slot offsets are those of
// the original objects; methods in between are not declared.
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZUnknown.h"

// (The record-access view with iid 0xc56b8f08 is cIGZPersistDBSerialRecord, see
// gz/cIGZPersistDBSerialRecord.h.)

// An opened database record (cGZPersistDBRecord). Slots 3..12 are the raw-access view of the
// record; the destructors follow (slots 13/14 of the original, declared by the concrete class).
// Slots 6..12 have no-op defaults so that minimal test doubles keep working.
class cIGZPersistDBRecord : public cIGZUnknown {
public:
    virtual cGZPersistResourceKey GetKey() const = 0;     // [3] +0x18
    virtual std::uint32_t GetAccessFlags() const = 0;     // [4] +0x20 (bit0 read, bit1 write)
    virtual bool Close() = 0;                             // [5] +0x28
    virtual bool GetFieldVoid(void*, std::uint32_t) { return false; }          // [6] +0x30
    virtual bool SetFieldVoid(const void*, std::uint32_t) { return false; }    // [7] +0x38
    virtual std::uint32_t GetSize() { return 0; }                              // [8] +0x40
    virtual bool SetSize(std::uint32_t) { return false; }                      // [9] +0x48
    virtual std::uint32_t GetPosition() { return 0; }                          // [10] +0x50
    virtual bool SeekAbsolute(std::uint32_t) { return false; }                 // [11] +0x58
    virtual bool SeekRelative(std::int32_t) { return false; }                  // [12] +0x60
};
constexpr std::uint32_t kGZIID_cIGZPersistDBRecord = 0xe56b8f03;

// The part of the persistence resource manager (service 0x056b906e, iid 0x656b8efc) the sound
// data sources use.
class cIGZPersistRecordService : public cIGZUnknown {
public:
    // +0x18: loads the persisted resource `key` as interface `iid` (the last two arguments are
    // flags the audio code always passes as 0).
    virtual bool GetResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out, std::uint32_t flags1, std::uint32_t flags2) = 0;
    // +0x28: creates a new resource object for `key` (the factory registered for the key's type);
    // the result is returned as interface `iid` and is not yet known to the manager.
    virtual bool CreateResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out, std::uint32_t flags1, std::uint32_t flags2) = 0;
    // +0x40 / +0x48 / +0x50: make a created resource known to the manager, remove it, test for it.
    virtual bool RegisterResource(cIGZUnknown* resource) = 0;
    virtual bool RemoveResource(const cGZPersistResourceKey& key) = 0;
    virtual bool HasResource(const cGZPersistResourceKey& key) = 0;
    // +0xf8: finds a record by key (returns an AddRef'd record on success).
    virtual bool FindRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** out) = 0;
    // +0x128: opens a record; `flags` 0 = read.
    virtual bool OpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** out, std::uint32_t flags) = 0;
};
constexpr std::uint32_t kGZSID_PersistResourceManager = 0x056b906e;
constexpr std::uint32_t kGZIID_PersistResourceManager = 0x656b8efc;
