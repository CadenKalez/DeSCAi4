// cGZSndDefaultEventHandler — the sound system's built-in event handler: plays / stops / kills
// sounds by id and edits the property cache. Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (vtable 0x100ab4ee0, 0x20-byte object, ctor @ 0x100479bf0, Create @ 0x10043cafa).
//
// Events (data1 is the sound id or property-cache instance):
//   1 start  — plays the sound at full volume once (consumed when the sound exists)
//   2 stop   3 reserved (data1 is read, nothing happens)   4 kill
//   0xca3553d6 add instance        0xca3553e7 remove instance
//   0xea355407 set instance property (data1 instance, data2 property, data3 value)
//   0x0a35540c remove instance property   0x0a355411 set global property (data1, data2)
//   0x0a355417 remove global property
// Only a successfully started sound reports the event as consumed.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIGZSndEventStd.h"
#include "audio/interfaces/cIGZSndMgr.h"
#include "audio/interfaces/cIGZSndSys.h"
#include "rz/cRZAutoRefCount.h"

class cGZSndDefaultEventHandler final : public cIGZSndEventHandler {
public:
    cGZSndDefaultEventHandler();                                       // @ 0x100479bf0
    static cGZSndDefaultEventHandler* Create() { return new cGZSndDefaultEventHandler(); }   // @ 0x10043cafa
    ~cGZSndDefaultEventHandler() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;       // ids 1, 0xbb523bb2 @ 0x10047a200
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;                                  // destroys at count 1
    bool DoEvent(cIGZSndEventStd* event) override;                     // @ 0x100479e2e

private:
    std::int32_t refCount_ = 0;                                        // +8
    cRZAutoRefCount<cIGZSndMgr> soundManager_;                         // +0x10
    cRZAutoRefCount<cIGZSndSys> soundSystem_;                          // +0x18
};
constexpr std::uint32_t kGZIID_cGZSndDefaultEventHandler = 0xbb523bb2;
