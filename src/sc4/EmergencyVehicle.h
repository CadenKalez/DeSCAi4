// cSC4EmergencyVehicle — the dispatched police car / fire engine / ambulance base (47 symbols at 0x1002f5580..0x1002f80d8
// plus SetCallNumber 0x10050f2d8 and GetDispatchDestination 0x10050f550; vtable 0x100a93ed0). A cSC4Vehicle that drives to a
// dispatch destination with its lights on, repaths when blocked, teleports when it cannot, and returns home.
//
// Object layout after cSC4Vehicle (which ends at +0x424):
//   +0x424 cS3DVector3 dispatch destination      +0x430 counted object (class 0x26bb767e, the dispatch source occupant)
//   +0x438 u8 repath counter                      +0x43a u16 call number
//   +0x43c u32 dispatch state (3 = driving to the call, 1 = blocked / at the call, 6 = finished)
//   +0x440 counted emergency-light model          +0x448 counted light effect
// Behaviour states: 100 en route, 0x65 repathing, 0x66 handling the event, 0x67 teleporting back; 0x34 is the pursuit state
// the base class already knows. Automaton flag 0x80 marks "emergency mode" (speed modifier ignored).
//
// RECOVERED here: ValidateNetworkOccupant, DoRepathingTick, SendPathBlockedMessage, SetDispatchState, constructor, Shutdown, Write / Read, the accessors, ShouldCollide, DoStateTick, DoEnRouteTick,
// DoAnimationTick, DoHandlingEventTick, DoPlayerDriveTick, UpdateAttraction, HandlePlayerDriveCommand, HandleEnterState,
// HandleExitState, HandleNetworkOccupantRemoved, ShowMotionBlur. NOT recovered yet (see notes/next_class_emergency_vehicle.md):
// Init, DoTeleportTick,
// HandleReachedRouteDestination, HandleDispatched, AppendRouteToNearbyRoad, ReturnToSourceOccupant, GetDebugExtraText,
// CloseEnoughToDispatchDestination, ShowEmergencyLights. They need the argument registers Ghidra lost; the disassembly decides.
#pragma once
#include <cstdint>

#include "sc4/Vehicle.h"

namespace sc4 {

class cSC4EmergencyVehicle;

constexpr std::uint32_t kEmergencyStateEnRoute = 100, kEmergencyStateRepathing = 0x65, kEmergencyStateHandlingEvent = 0x66,
                        kEmergencyStateTeleport = 0x67;
constexpr std::uint32_t kEmergencyDispatchedObjectIid = 0x26bb767e;     // cISC4Occupant: SC4ReadGZSerializable id of +0x430
constexpr std::uint32_t kEmergencySoundStopMessage = 0x6a451908;        // sound system vslot 0x50: siren off for an automaton
constexpr std::uint32_t kEmergencySoundMoveMessage = 0xca45190f;        // siren position update
constexpr std::uint32_t kEmergencyPlayerDriveLightsCommand = 9;         // the key that toggles the lights while driving it

// The emergency tunables (cSC4AutomataManager::sTuningParams + 0x170..0x17c); zero until the tuning loader (unrecovered) runs.
struct EmergencyVehicleTuning {
    float retrySeconds = 0.0f;           // +0x170 (the retry state's timer is this * 1000)
    std::uint8_t teleportCells = 0;      // +0x174 (teleport when farther than this many cells)
    std::uint8_t maxRepaths = 0;         // +0x175
    std::uint32_t speedKmh = 0;          // +0x178 (the emergency speed, in km/h)
    std::int32_t surroundingDepth = 0;   // +0x17c (RemoveSurroundingAutomata's depth)
};

// The sound system and light services the vehicle reaches (SL::spSS, SL::spCity effects manager).
class IEmergencyVehicleHost {
public:
    virtual ~IEmergencyVehicleHost() = default;
    virtual void SirenStopped(const cSC4EmergencyVehicle& vehicle) = 0;                            // spSS vslot 0x50(0x6a451908, id, 0, 0, 0)
    virtual void SirenMoved(const cSC4EmergencyVehicle& vehicle, int x, int y, int z) = 0;         // spSS vslot 0x50(0xca45190f, id, x, y, z)
    virtual void SetEmergencyLights(cSC4EmergencyVehicle& vehicle, bool on) = 0;                    // ShowEmergencyLights (effects + model)
    virtual const EmergencyVehicleTuning& Tuning() = 0;
    virtual void SirenStarted(const cSC4EmergencyVehicle& vehicle, std::uint32_t soundId) = 0;      // spSS vslot 0x50(0xea451903, soundId, id, 0, 0)
    // message 0xca8dcdb4: Data1 = dispatch type, Data2 = repath count, IGZUnknown = the vehicle's occupant; posted on the message server
    virtual void PostPathBlocked(cSC4EmergencyVehicle& vehicle, std::uint32_t dispatchType, std::uint8_t repathCount) = 0;
    virtual ITrafficAutomatonOccupant* NetworkOccupantAt(float x, float z) = 0;                     // spCity position -> cell, network manager vslot 0x38
    // spCity vslot 0x158 (the route finder) vslot 0x90(vehicle, list, request{1, destination}, 0, 3): true when a route was found
    virtual bool FindRoute(cSC4EmergencyVehicle& vehicle, const cS3DVector3& destination) = 0;
};

class cSC4EmergencyVehicle : public cSC4Vehicle {
public:
    explicit cSC4EmergencyVehicle(ITrafficAutomatonHost* host = nullptr, IAutomatonHost* automatonHost = nullptr,
                                  IVehicleHost* vehicleHost = nullptr, IEmergencyVehicleHost* emergencyHost = nullptr,
                                  const cGZPersistResourceKey& modelKey = {});
    ~cSC4EmergencyVehicle() override;

    bool Shutdown() override;                                                // 0x20  @ 0x1002f56dc
    bool Write(cIGZOStream& stream) const override;                          // 0x240 @ 0x1002f7f02
    bool Read(cIGZIStream& stream) override;                                 // 0x248 @ 0x1002f7fbc
    bool DoAnimationTick(std::uint32_t unknown, float milliseconds) override;   // 0x278 @ 0x1002f576e
    bool ShouldCollide(cSC4Automaton* other) const override;                 // 0x280 @ 0x1002f78e6
    bool DoStateTick(float milliseconds) override;                           // 0x3a0 @ 0x1002f57fc
    bool HandleEnterState(tAutomataState* state) override;                   // 0x3a8 @ 0x1002f603c
    bool HandleExitState(const tAutomataState* state) override;              // 0x3b0 @ 0x1002f611e
    bool HandlePlayerDriveCommand(std::uint32_t command, bool on) override;  // 0x1e0 @ 0x1002f600c
    bool DoPlayerDriveTick(float milliseconds) override;                     // 0x3d0 @ 0x1002f5f9e
    void HandleNetworkOccupantRemoved() override;                            // 0x470 @ 0x1002f62e6
    void UpdateAttraction() override;                                        // 0x380 @ 0x1002f5fa8
    void ShowMotionBlur(bool on) override { (void)on; }                      // 0x5c0 @ 0x1002f7efa (does nothing)
    std::uint16_t GetCallNumber() const override { return callNumber_; }     // 0xb8  @ 0x1002f80a6
    void SetCallNumber(std::uint16_t number) override { callNumber_ = number; }   // 0xc0 @ 0x10050f2d8
    std::uint32_t GetDispatchState() const override { return dispatchState_; }    // 0xd0 @ 0x1002f80c2
    const cS3DVector3& GetDispatchDestination() const { return dispatchDestination_; }   // 0x4b0 @ 0x10050f550

    // ---- state handlers (slots 0x650..0x668 in the original) ----
    virtual bool DoEnRouteTick(float milliseconds);                          // 0x650 @ 0x1002f58d4
    virtual bool DoRepathingTick(float milliseconds);                         // 0x658 @ 0x1002f5ae2
    virtual bool DoHandlingEventTick(float milliseconds) { (void)milliseconds; return true; }   // 0x660 @ 0x1002f5d00
    virtual bool DoTeleportTick(float milliseconds);                          // 0x668 @ 0x1002f5d08 (NOT RECOVERED: declared, defers)
    bool ValidateNetworkOccupant();                                           // @ 0x1002f5a26
    void SendPathBlockedMessage();                                            // @ 0x1002f5c36
    void SetDispatchState(std::uint32_t state) override;                      // 0xc8  @ 0x1002f796c
    virtual std::uint32_t GetDispatchType() const = 0;                        // 0x638 (pure: Ambulance, GetawayVan, ... answer it)
    virtual void ShowEmergencyLights(bool on);                               // 0x648 (seam: IEmergencyVehicleHost)

    // ---- accessors for the tests and the subclasses ----
    void SetDispatchDestination(const cS3DVector3& destination) { dispatchDestination_ = destination; }
    void SetDispatchStateRaw(std::uint32_t state) { dispatchState_ = state; }
    cIGZUnknown* DispatchSource() const { return dispatchSource_; }
    void SetDispatchSource(cIGZUnknown* source);
    std::uint8_t RepathCount() const { return repathCount_; }
    void SetRepathCount(std::uint8_t count) { repathCount_ = count; }
    IAutomatonModel* LightModel() const { return lightModel_; }
    void SetLightModel(IAutomatonModel* model) { lightModel_ = model; }
    IEmergencyVehicleHost* EmergencyHost() const { return emergencyHost_; }

protected:
    IEmergencyVehicleHost* emergencyHost_ = nullptr;      // port seam (not in the original)
    cS3DVector3 dispatchDestination_{};                   // +0x424
    cIGZUnknown* dispatchSource_ = nullptr;               // +0x430 counted
    std::uint8_t repathCount_ = 0;                        // +0x438
    std::uint16_t callNumber_ = 0;                        // +0x43a
    std::uint32_t dispatchState_ = 0;                     // +0x43c
    IAutomatonModel* lightModel_ = nullptr;               // +0x440 counted
    cIGZUnknown* lightEffect_ = nullptr;                  // +0x448 counted
};

}  // namespace sc4
