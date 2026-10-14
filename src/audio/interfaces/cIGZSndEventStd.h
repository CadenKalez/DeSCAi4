// cIGZSndEventStd / cIGZSndEventHandler — the sound system's event objects and their receivers.
// cGZSndEventStd (vtable 0x100adf560) carries a cRZMessage2Standard payload at +0 and exposes the
// cIGZSndEventStd interface at +0x50: slots 3..6 (+0x18..+0x30) are HasEventId/GetEventId/
// SetEventId/AsMessage2Standard. Event ids double as cIGZMessage2 types.
#pragma once
#include <cstdint>

#include "gz/cIGZMessage2.h"

class cIGZSndEventStd : public cIGZUnknown {
public:
    virtual bool GetHasEventId() const = 0;                       // [3] +0x18
    virtual std::uint32_t GetEventId() const = 0;                 // [4] +0x20
    virtual void SetEventId(std::uint32_t id) = 0;                // [5] +0x28
    virtual cIGZMessage2Standard* AsMessage2Standard() = 0;       // [6] +0x30
};

constexpr std::uint32_t kGZCLSID_cGZSndEventStd = 0x5b523ba4;     // class id passed to EventCreate

// Receiver registered with cIGZSndSys::RegisterEventHandler (slot 3, +0x18). A non-zero return
// means the event was consumed.
class cIGZSndEventHandler : public cIGZUnknown {
public:
    virtual bool DoEvent(cIGZSndEventStd* event) = 0;             // [3] +0x18
};
