// cSC4TrafficEffectManager — the list of live traffic effects (accidents and similar events that block cells and attract
// emergency vehicles). 11 functions, 0x100111eaa..0x10011b768. Each effect (cSC4TrafficEffect, 0x40 bytes at list node +0x10) has a
// centre cell, a radius, two bounding rectangles used by GetPopulateFlags, the vehicle occupant it follows, a flag saying a
// controller is attached to that occupant and the time left in milliseconds (0xffffffff = until stopped).
// Original layout: +0 / +8 list sentinel, +0x10 count, +0x18 cRZTimer (units 4), +0x30 next update time, +0x34 last update time.
// The city (cell conversion, the controller service at SL::spCity slot 0x178) and the clock go through ITrafficEffectHost.
// Streams: version u16 = 1, count u32, per effect x s32, z s32, radius u32, rect, rect, occupant (GZ serializable 0x26bb767e), bool, u32.
#pragma once
#include <cstdint>
#include <list>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "sc4/MiscTypes.h"
#include "sc4/Occupant.h"

namespace sc4 {

struct TrafficEffect {
    int x = 0, z = 0;                       // +0x10 / +0x14 (centre cell)
    int radius = 0;                         // +0x18
    SC4RectInt32 nearRect{}, farRect{};     // +0x1c (radius - 1) / +0x2c (radius)
    cISC4Occupant* occupant = nullptr;      // +0x40 (AddRef'd)
    bool controllerAttached = false;        // +0x48
    std::uint32_t timeLeft = 0;             // +0x4c milliseconds
};

class ITrafficEffectHost {
public:
    virtual ~ITrafficEffectHost() = default;
    virtual std::uint32_t ElapsedMilliseconds() = 0;                                       // the manager's cRZTimer
    virtual void PositionToCell(float x, float z, int& cellX, int& cellZ) = 0;             // SL::spCity slot 0x2f8
    // SL::spCity slot 0x178 service: slot 0x48 (group "kTrafficAccidentControllerGroupName", occupant, position) / slot 0x70
    virtual bool AttachAccidentController(cISC4Occupant* occupant, const float* position) = 0;
    virtual void DetachController(cISC4Occupant* occupant) = 0;
};

class cSC4TrafficEffectManager {
public:
    explicit cSC4TrafficEffectManager(ITrafficEffectHost* host) : host_(host) {}   // 0x10011b768
    ~cSC4TrafficEffectManager() { Shutdown(); }

    void Shutdown();                                                  // 0x100111eaa
    void Update();                                                    // 0x100112e0e: every 500 ms, ages effects and follows vehicles
    std::uint8_t GetPopulateFlags(int cellX, int cellZ) const;        // 0x100113ab4: bit 0 inside the near rect, bit 1 the far rect
    TrafficEffect* CreateAccident(cISC4Occupant* vehicle, int radius, std::uint32_t durationMs);   // 0x100113baa
    TrafficEffect* CreateEffect(int cellX, int cellZ, int radius, cISC4Occupant* occupant);        // 0x100113d40
    bool StopEffect(TrafficEffect* effect);                           // 0x100113e6a: always true
    bool Write(cIGZOStream& stream) const;                            // 0x1001160f2
    bool Read(cIGZIStream& stream);                                   // 0x10011627c
    bool ShutdownEffect(TrafficEffect& effect);                       // 0x10011b568: detaches the controller, drops the occupant
    bool GetMostRecentAccidentLocation(int& cellX, int& cellZ) const; // 0x10011b6b6

    const std::list<TrafficEffect>& Effects() const { return effects_; }
    std::uint32_t NextUpdate() const { return nextUpdate_; }

private:
    void SetRects(TrafficEffect& e) const;
    static void SetOccupant(TrafficEffect& e, cISC4Occupant* occupant);
    ITrafficEffectHost* host_;
    std::list<TrafficEffect> effects_;
    std::uint32_t nextUpdate_ = 0;   // +0x30
    std::uint32_t lastUpdate_ = 0;   // +0x34
};

}  // namespace sc4
