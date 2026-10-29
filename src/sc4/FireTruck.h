// cSC4FireTruck — the fire engine (vtable 0x100abfc60, class id 0xaa222b83, dispatch type 1, 0x468 bytes): a cSC4EmergencyVehicle that
// carries a fire crew (cSC4SimFireCrew, 0x438 bytes, unrecovered) to a fire or a toxic spill, and drives to the nearest fire it has been
// pointed at when the player drives it. Fields after the emergency vehicle: +0x450 and +0x458 are two counted occupant filters the truck
// builds in its constructor (the fire filter and the spill filter), +0x460 the counted fire crew.
//
// The fire protection simulator, the toxic spill manager, the fire occupants, the occupant manager and the renderer are behind IFireTruckHost.
#pragma once
#include <cstdint>

#include "sc4/EmergencyVehicle.h"

namespace sc4 {

constexpr std::uint32_t kFireTruckGZCLSID = 0xaa222b83;
constexpr std::uint32_t kFireTruckDispatchType = 1;
constexpr float kFireTruckDefaultRadius = 16.0f;                    // 0x41800000
constexpr float kFireStopSpeed = 0.001f;                            // 0x100a16da0: below this the engine counts as stopped
constexpr float kSpillRadiusScale = 3.0f * 0.0625f;                 // 0x100a165b8 * 0x100a158b0: cells per radius unit
constexpr std::uint32_t kFireCountServiceId = 0x621cda33, kFireCountInterfaceId = 0x49b43f57;      // the fire manager behind SL::spCity vslot 0x240
constexpr std::uint32_t kSpillServiceId = 0xebfb0404, kSpillInterfaceId = 0x4c1e8345;
constexpr std::uint32_t kFireTruckCommandFire = 7, kFireTruckCommandStop = 8;   // the player-drive keys the truck reacts to

class cSC4FireTruck;

class IFireTruckHost {
public:
    virtual ~IFireTruckHost() = default;
    virtual cIGZUnknown* NewFireFilter() = 0;                       // the constructor's first filter (vtable 0x100ab1fb0), reference count zero
    virtual cIGZUnknown* NewSpillFilter() = 0;                      // the second (vtable 0x100ae24d0)
    // spFireProtectionSim vslot 0x38(source) says whether the station is known, vslot 0x50(source) is its competence
    virtual bool FireCompetence(cIGZUnknown* source, float& competence) = 0;
    // spCity vslot 0x1c8 -> vslot 0x38(source) / vslot 0xc0(source): the station's reach
    virtual bool StationRadius(cIGZUnknown* source, float& radius) = 0;
    virtual int FireCount() = 0;                                    // the fire manager (0x621cda33 / 0x49b43f57) vslot 0x58 -> vslot 0x80, 0 when absent
    // occupant manager (spCity vslot 0x110) vslot 0x128(list, min, max, filter, 0xffffffff): the occupants inside the box; false when none
    virtual bool OccupantsInBox(const float min[3], const float max[3], cIGZUnknown* filter, int& count) = 0;
    virtual int SpillTotal() = 0;                                   // the spill manager (0xebfb0404 / 0x4c1e8345) vslot 0x28
    virtual int SpillsInRect(const int rect[4]) = 0;                // the same object's vslot 0x20(rect: x0, z0, x1, z1)
    virtual cIGZUnknown* NewFireCrew(cSC4FireTruck& truck) = 0;     // new cSC4SimFireCrew(truck), reference count zero
    virtual void SetFireCrewCompetence(cIGZUnknown* crew, float competence) = 0;
    virtual void RegisterFireCrew(cIGZUnknown* crew) = 0;           // spCity vslot 0x160 -> vslot 0x38
    virtual void SetupFireCrew(cIGZUnknown* crew) = 0;
    virtual void RemoveFireCrew(cIGZUnknown* crew) = 0;             // spCity vslot 0x160 -> vslot 0x40(crew, 0)
    virtual bool PickGroundPoint(float screenX, float screenY, cS3DVector3& out) = 0;   // SL::spRenderer vslot 0x1e8(x, y, out, 1, 1)
    virtual bool CityCells(int& width, int& height) = 0;            // spCity vslots 0x2e8 / 0x2f0; false without a city
    virtual float MaxStreamDistance() = 0;                          // sTuningParams + 0x198
};

class cSC4FireTruck : public cSC4EmergencyVehicle {
public:
    explicit cSC4FireTruck(ITrafficAutomatonHost* host = nullptr, IAutomatonHost* automatonHost = nullptr,
                           IVehicleHost* vehicleHost = nullptr, IEmergencyVehicleHost* emergencyHost = nullptr,
                           IFireTruckHost* fireHost = nullptr, const cGZPersistResourceKey& modelKey = {});   // @ 0x10050f670
    ~cSC4FireTruck() override;                                                    // @ 0x100510816

    bool Shutdown() override;                                                     // 0x20  @ 0x10050f926
    bool ChooseNewDestination() override;                                         // 0x560 @ 0x10050f9ec
    void SetDispatchState(std::uint32_t state) override;                          // 0xc8  @ 0x10050fbf2
    bool HandlePlayerDriveCommand(std::uint32_t command, bool on) override;       // 0x1e0 @ 0x1005102c2
    bool HandleExitState(const tAutomataState* state) override;                   // 0x3b0 @ 0x100510444
    bool TranslatePlayerDriveCommands(tAutomataStatePlayerDrive* state, float milliseconds) override;   // 0x400 @ 0x100510474
    bool Write(cIGZOStream& stream) const override;                               // 0x240 @ 0x1005106d4
    bool Read(cIGZIStream& stream) override;                                      // 0x248 @ 0x10051074c
    bool DoHandlingEventTick(float milliseconds) override;                        // 0x660 @ 0x1005101fc
    std::uint32_t GetGZCLSID() const override { return kFireTruckGZCLSID; }       // 0x250 @ 0x10051084e
    std::uint32_t GetDispatchType() const override { return kFireTruckDispatchType; }   // 0x638 @ 0x100510866
    float GetEffectiveRadius() const override;                                    // 0x640 @ 0x10051019c

    virtual void HandleCrewFinished();                                            // 0x670 @ 0x10050fbb2
    bool GetStreamTargetPoint(cS3DVector3& out);                                  // @ 0x100510680
    void GetFireCount(int& citywide, int& nearby);                                // @ 0x10050fd50
    void GetToxicSpillCount(int& citywide, int& nearby);                          // @ 0x10050ff3c
    void ReleaseFireCrew();                                                       // @ 0x10050f97c
    void CreateFireCrew();                                                        // @ 0x100510060

    cIGZUnknown* FireCrew() const { return crew_; }
    cIGZUnknown* FireFilter() const { return fireFilter_; }
    cIGZUnknown* SpillFilter() const { return spillFilter_; }

private:
    void SetCrew(cIGZUnknown* crew);

    IFireTruckHost* fireHost_ = nullptr;        // port seam (not in the original)
    cIGZUnknown* fireFilter_ = nullptr;         // +0x450 counted
    cIGZUnknown* spillFilter_ = nullptr;        // +0x458 counted
    cIGZUnknown* crew_ = nullptr;               // +0x460 counted
};

}  // namespace sc4
