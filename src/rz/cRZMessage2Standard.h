// cRZMessage2Standard — the standard message: a type plus up to four integer/pointer payloads, a
// string, an unknown and an extra serializable (each with a "has" flag). Recovered from Sim City 4
// Deluxe Editionsub 1.2.1 (vtable 0x100a68f80, 0x50-byte object, ctor @ 0x1000412ce).
//
// Layout: refcount +8, flags +0xc (bit0 type set, 1..4 data1..4, 5 extra, 6 string, 7 unknown),
// type +0x10, data1..4 at +0x18..+0x30 (8 bytes each), string +0x38, unknown +0x40, extra +0x48.
// Created with count 0; Release destroys at counts <= 1.
#pragma once
#include <cstdint>

#include "gz/cIGZMessage2.h"
#include "rz/cRZAutoRefCount.h"

class cRZMessage2Standard : public cIGZMessage2Standard {
public:
    cRZMessage2Standard() = default;        // @ 0x1000412ce
    virtual ~cRZMessage2Standard();         // @ 0x1000413a2 releases extra, unknown, string

    bool QueryInterface(std::uint32_t iid, void** out) override;   // ids 1, 0xa52294b4, 0x65297976 @ 0x100041468
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    bool Create(std::uint32_t iid, void** out) const override;     // @ 0x1000417d4: new message of the same type
    std::uint32_t GetType() const override { return type_; }
    void SetType(std::uint32_t type) override;                      // @ 0x1000414d6: flag bit0 = type != 0
    bool IsEqual(const cIGZMessage2& other) const override { return type_ == other.GetType(); }
    bool IsLess(const cIGZMessage2& other) const override { return type_ < other.GetType(); }

    bool GetHasData1() const override { return (flags_ & 0x02) != 0; }
    std::uint64_t GetData1() const override { return data_[0]; }
    void* GetVoid1() const override { return reinterpret_cast<void*>(data_[0]); }
    void SetData1(std::uint64_t value) override { data_[0] = value; flags_ |= 0x02; }
    void SetVoid1(void* value) override { SetData1(reinterpret_cast<std::uint64_t>(value)); }
    bool GetHasData2() const override { return (flags_ & 0x04) != 0; }
    std::uint64_t GetData2() const override { return data_[1]; }
    void* GetVoid2() const override { return reinterpret_cast<void*>(data_[1]); }
    void SetData2(std::uint64_t value) override { data_[1] = value; flags_ |= 0x04; }
    void SetVoid2(void* value) override { SetData2(reinterpret_cast<std::uint64_t>(value)); }
    bool GetHasData3() const override { return (flags_ & 0x08) != 0; }
    std::uint64_t GetData3() const override { return data_[2]; }
    void* GetVoid3() const override { return reinterpret_cast<void*>(data_[2]); }
    void SetData3(std::uint64_t value) override { data_[2] = value; flags_ |= 0x08; }
    void SetVoid3(void* value) override { SetData3(reinterpret_cast<std::uint64_t>(value)); }
    bool GetHasData4() const override { return (flags_ & 0x10) != 0; }
    std::uint64_t GetData4() const override { return data_[3]; }
    void* GetVoid4() const override { return reinterpret_cast<void*>(data_[3]); }
    void SetData4(std::uint64_t value) override { data_[3] = value; flags_ |= 0x10; }
    void SetVoid4(void* value) override { SetData4(reinterpret_cast<std::uint64_t>(value)); }

    bool GetHasString() const override { return (flags_ & 0x40) != 0; }
    bool GetString(std::uint32_t iid, void** out) override;         // @ 0x1000415f6
    void SetString(cIGZString* value) override;                     // @ 0x100041640
    bool GetHasIGZUnknown() const override { return (flags_ & 0x80) != 0; }
    cIGZUnknown* GetIGZUnknown() override { return unknown_; }
    void SetIGZUnknown(cIGZUnknown* value) override;                // @ 0x1000416b8
    bool GetHasExtra() const override { return (flags_ & 0x20) != 0; }
    bool GetExtra(std::uint32_t iid, void** out) override;          // @ 0x100041728
    void SetExtra(cIGZSerializable* value) override;                // @ 0x100041772

private:
    std::uint32_t refCount_ = 0;                  // +8
    std::uint8_t flags_ = 0;                      // +0xc
    std::uint32_t type_ = 0;                      // +0x10
    std::uint64_t data_[4] = {0, 0, 0, 0};       // +0x18
    cIGZString* string_ = nullptr;                // +0x38 (referenced)
    cIGZUnknown* unknown_ = nullptr;              // +0x40 (referenced)
    cIGZSerializable* extra_ = nullptr;           // +0x48 (referenced)
};
