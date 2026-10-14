// Persistence-layer interfaces the audio data sources use to read resources out of packed
// databases. The persistence layer (cGZPersistResourceManager, cGZPersistDBRecord, ...) is not
// recovered yet, so only the calls the audio code makes are declared. Slot offsets are those of
// the original objects; methods in between are not declared.
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZUnknown.h"

// An opened database record (cGZPersistDBRecord). Slot 5 (+0x28) closes it.
class cIGZPersistDBRecord : public cIGZUnknown {
public:
    virtual cGZPersistResourceKey GetKey() const = 0;     // [3] +0x18
    virtual std::uint32_t GetAccessFlags() const = 0;     // [4] +0x20
    virtual bool Close() = 0;                             // [5] +0x28
};

// Sequential access view of a record (interface id 0xc56b8f08).
class cIGZPersistRecordAccess : public cIGZUnknown {
public:
    // +0x68: reads `count` 32-bit words (little endian) into `destination`.
    virtual bool ReadUint32s(std::uint32_t* destination, std::uint32_t count) = 0;
    // +0xd8: reads exactly `byteCount` bytes.
    virtual bool ReadBytes(void* destination, std::uint32_t byteCount) = 0;
    // +0x1a0: the record's total size in bytes.
    virtual std::uint32_t GetSize() const = 0;
};
constexpr std::uint32_t kGZIID_cIGZPersistRecordAccess = 0xc56b8f08;

// The part of the persistence resource manager (service 0x056b906e, iid 0x656b8efc) the sound
// data sources use.
class cIGZPersistRecordService : public cIGZUnknown {
public:
    // +0x18: loads the persisted resource `key` as interface `iid` (the last two arguments are
    // flags the audio code always passes as 0).
    virtual bool GetResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out, std::uint32_t flags1, std::uint32_t flags2) = 0;
    // +0xf8: finds a record by key (returns an AddRef'd record on success).
    virtual bool FindRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** out) = 0;
    // +0x128: opens a record; `flags` 0 = read.
    virtual bool OpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** out, std::uint32_t flags) = 0;
};
constexpr std::uint32_t kGZSID_PersistResourceManager = 0x056b906e;
constexpr std::uint32_t kGZIID_PersistResourceManager = 0x656b8efc;
