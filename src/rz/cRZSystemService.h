// cRZSystemService — base class of framework services. Layout (0x28 bytes): +0 cIGZSystemService
// subobject, +8 cRZUnknown subobject (vptr, refcount at +0x10), +0x14 service id, +0x18 priority,
// +0x1c tick priority, +0x20 running flag. Recovered from vtable 0x100a688b0 (ctor @ 0x10003d14a/0x10003d1ac).
#pragma once
#include <cstdint>

#include "gz/cIGZSystemService.h"
#include "rz/cRZUnknown.h"

class cRZSystemService : public cIGZSystemService, public cRZUnknown {
public:
    // (id, priority): the tick priority starts equal to the priority. @ 0x10003d14a
    cRZSystemService(std::uint32_t serviceId, std::int32_t priority)
        : serviceId_(serviceId), priority_(priority), tickPriority_(priority) {}
    // (id, priority, tickPriority) @ 0x10003d1ac
    cRZSystemService(std::uint32_t serviceId, std::int32_t priority, std::int32_t tickPriority)
        : serviceId_(serviceId), priority_(priority), tickPriority_(tickPriority) {}

    // One override serves both bases (the binary has a thunk for the cRZUnknown subobject).
    bool QueryInterface(std::uint32_t iid, void** out) override {  // @ 0x10003d286
        if (iid == kGZIID_cIGZSystemService) {
            *out = static_cast<cIGZSystemService*>(this);
            AddRef();
            return true;
        }
        return cRZUnknown::QueryInterface(iid, out);
    }
    std::uint32_t AddRef() override { return cRZUnknown::AddRef(); }     // @ 0x10003d2ce
    std::uint32_t Release() override { return cRZUnknown::Release(); }   // @ 0x10003d2e6

    std::uint32_t GetServiceID() const override { return serviceId_; }   // @ 0x10003d2fe
    void SetServiceID(std::uint32_t id) override { serviceId_ = id; }    // @ 0x10003d308
    std::int32_t GetServicePriority() const override { return priority_; }  // @ 0x10003d312
    bool IsServiceRunning() const override { return running_; }          // @ 0x10003d31c
    void SetServiceRunning(bool running) override { running_ = running; }  // @ 0x10003d326
    bool Init() override { return true; }                                // @ 0x10003d276
    bool Shutdown() override { return true; }                            // @ 0x10003d27e
    bool OnTick(std::uint32_t) override { return true; }                 // @ 0x10003d338
    bool OnIdle(std::uint32_t) override { return true; }                 // @ 0x10003d330
    using cIGZSystemService::GetServiceTickPriority;
    virtual std::int32_t GetServiceTickPriority() const { return tickPriority_; }  // [13] @ 0x10003d340

    ~cRZSystemService() override = default;

private:
    std::uint32_t serviceId_;
    std::int32_t priority_;
    std::int32_t tickPriority_;
    bool running_ = false;
};
