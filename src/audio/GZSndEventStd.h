// cGZSndEventStd — the sound system's event object: a cRZMessage2Standard (type 0xdb53383b) that
// also carries an event id. Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable
// 0x100adf560, 0x60-byte object; the cIGZSndEventStd interface lives at +0x50, event id +0x58,
// has-event-id flag +0x5c).
//
// Differences from a plain standard message (all stubs in the binary): the Void accessors do not
// store anything (Get returns null), the unknown and extra payloads report "not present" from
// GetHas*, and SetExtra is ignored. GetIGZUnknown / SetIGZUnknown still work.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIGZSndEventStd.h"
#include "rz/cRZMessage2Standard.h"

constexpr std::uint32_t kGZMessageTypeSndEvent = 0xdb53383b;   // type given by the constructors

class cGZSndEventStd final : public cRZMessage2Standard, public cIGZSndEventStd {
public:
    cGZSndEventStd();                                          // @ 0x10069a140
    // @ 0x10069a240: event id and the four data words.
    cGZSndEventStd(std::uint32_t eventId, std::uint32_t data1, std::uint32_t data2, std::uint32_t data3, std::uint32_t data4);
    ~cGZSndEventStd() override = default;

    // QueryInterface: 0x5b523ba4 -> the cIGZSndEventStd part, anything else as cRZMessage2Standard.
    bool QueryInterface(std::uint32_t iid, void** out) override;       // @ 0x10069a5f0
    std::uint32_t AddRef() override { return cRZMessage2Standard::AddRef(); }       // @ 0x10069a6a0
    std::uint32_t Release() override { return cRZMessage2Standard::Release(); }     // @ 0x10069a6c0

    void* GetVoid1() const override { return nullptr; }                // @ 0x10069a6e0
    void SetVoid1(void*) override {}                                   // @ 0x10069a6f0
    void* GetVoid2() const override { return nullptr; }
    void SetVoid2(void*) override {}
    void* GetVoid3() const override { return nullptr; }
    void SetVoid3(void*) override {}
    void* GetVoid4() const override { return nullptr; }
    void SetVoid4(void*) override {}
    bool GetHasIGZUnknown() const override { return false; }           // @ 0x10069a760
    bool GetHasExtra() const override { return false; }                // @ 0x10069a780
    void SetExtra(cIGZSerializable*) override {}                       // @ 0x10069a7a0

    bool GetHasEventId() const override { return hasEventId_; }        // @ 0x10069a7b0
    std::uint32_t GetEventId() const override { return eventId_; }     // @ 0x10069a7d0
    void SetEventId(std::uint32_t id) override { eventId_ = id; hasEventId_ = true; }   // @ 0x10069a7f0
    cIGZMessage2Standard* AsMessage2Standard() override { return this; }   // @ 0x10069a810

private:
    std::uint32_t eventId_ = 0;          // +0x58
    bool hasEventId_ = false;            // +0x5c
};
