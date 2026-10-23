// cISC4AEMSEvents — the game-side interface of the AEMS (the game's audio event system). AEMS turns
// numbered game events (fire, disaster, UI, ambient triggers, vehicle loops) into sound calls: the
// game starts an event index and later ends it, and the engine mixes the matching loops. Every
// event index is also a slot in the system's 21-entry state array.
//
// Recovered from the vtable group at +8 of cSC4AudioAEMSEventSystem (vtable 0x100af05c0, iid
// 0x8c1faeb7, service id 0xac1fae31). Slots: [3] +0x18 LoadSoundBank, [4] +0x20 UnloadSoundBank,
// [5] +0x28 StartSound, [6] +0x30 EndSound, [7] +0x38 SetParamVelocity, [8] +0x40 SetParamThrottle,
// [9] +0x48 SetParamWobble. The three SetParam calls take an SC4Percentage (a float in xmm0).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

namespace sc4 {
class cISCPropertyHolder;
struct SC4Percentage;
}  // namespace sc4

// The interface id of the +8 view (SL::AEMS() in ServiceLocator.cpp asks 0xac1fae31 / 0x8c1faeb7).
constexpr std::uint32_t kGZIID_cISC4AEMSEvents = 0x8c1faeb7;

class cISC4AEMSEvents : public cIGZUnknown {
public:
    // The AEMS event indices. The system keeps one state slot per index (21 in total, 0x00..0x14);
    // 0x0f and 0x10 are valid indices but StopAllSounds never ends them.
    enum AEMSEventType : std::uint32_t {
        kAEMSEventCount = 0x15,
        // Known users (Vehicle.h / Helicopter.h): 0x05 off-road, 0x07 collision, 0x0e tunnel,
        // 0x10 driving, 0x12 crash; 0x4308 in the helicopter is an AEMS group id, not a slot.
    };

    // Both bank calls read `propertyId` from the exemplar the caller holds; the value is the bank
    // resource instance id (default 0xebf44df7 when the property is missing).
    virtual void LoadSoundBank(const sc4::cISCPropertyHolder* holder, std::uint32_t propertyId) = 0;    // [3] +0x18
    virtual void UnloadSoundBank(const sc4::cISCPropertyHolder* holder, std::uint32_t propertyId) = 0;  // [4] +0x20
    virtual bool StartSound(AEMSEventType event) = 0;                            // [5] +0x28
    virtual bool EndSound(AEMSEventType event) = 0;                              // [6] +0x30
    virtual bool SetParamVelocity(sc4::SC4Percentage percentage) = 0;            // [7] +0x38
    virtual bool SetParamThrottle(sc4::SC4Percentage percentage) = 0;            // [8] +0x40
    virtual bool SetParamWobble(sc4::SC4Percentage percentage) = 0;              // [9] +0x48
};
