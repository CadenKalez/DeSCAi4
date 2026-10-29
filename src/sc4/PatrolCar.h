// cSC4PatrolCar — the police car (vtable 0x100abf2e0, class id 0x4a222b98, 0x458 bytes): a cSC4EmergencyVehicle that arrests the
// criminals around it and, during a riot, brings a riot-police crew. 0x450 holds the counted riot-police automaton.
//
// Behaviour states: 0x68 is the arrest state (a four-model animation that ends in a pop); state 7 is the idle tick every other
// emergency state also uses. Dispatch states the car reacts to: 1 (blocked), 2 (finished), 5 (at the call).
//
// The police simulator, the crime simulator, the riot-police automaton (cSC4RiotPolice, 0x3c8 bytes, unrecovered) and the
// animation replacement are reached through IPatrolCarHost, in the order the original calls them.
#pragma once
#include <cstdint>

#include "sc4/EmergencyVehicle.h"

namespace sc4 {

constexpr std::uint32_t kPatrolCarGZCLSID = 0x4a222b98;
constexpr std::uint32_t kPatrolCarDispatchType = 0;
constexpr float kPatrolCarDefaultRadius = 16.0f;                   // 0x41800000
constexpr std::uint32_t kPatrolStateArrest = 0x68;
constexpr float kCrimeSearchRadius = 20.0f;                        // 0x41a00000
constexpr float kPatrolIdleStateSeconds = 2000.0f;                 // 0x44fa0000: the timer of the state pushed on arrival
constexpr float kCriminalFadeOpacity = 0.0f, kCriminalFadeSeconds = 0.2f;   // 0x100a18178: SetOpacity(0, 0.2f)
constexpr float kCriminalLeaveStateSeconds = 5000.0f;              // 0x459c4000
constexpr int kArrestParticipantsMax = 5;
// The arrest animation instance by the police competence (kArrestAnimationInstance @ 0x100a23800, index = (int)(competence * 100 / 40)).
constexpr std::uint32_t kArrestAnimationInstance[4] = {0x03400000, 0x03200000, 0x03300000, 0x0b500000};

// What the crime simulator hands the car (vslot 0x50 of spCrimeSim): somebody who can be arrested.
class ICrimeParticipant {
public:
    virtual ~ICrimeParticipant() = default;
    virtual void Arrest(bool on) = 0;                              // vslot 0x58
};

class cSC4PatrolCar;

class IPatrolCarHost {
public:
    virtual ~IPatrolCarHost() = default;
    virtual bool RiotInProgress() = 0;                              // spCity vslot 0x240 -> vslot 0x50
    // spPoliceSim vslot 0xb0(source) says whether the source station is known; vslot 0xc8(source) is its competence (0..1)
    virtual bool PoliceCompetence(cIGZUnknown* source, float& competence) = 0;
    // spCity vslot 0x208 -> vslot 0xb0(source) / vslot 0xb8(source): the station's reach, in cells
    virtual bool StationRadius(cIGZUnknown* source, float& radius) = 0;
    virtual cSC4Automaton* FindCrimeAutomaton(const cS3DVector3& position, float radius) = 0;   // spCrimeSim vslot 0x48
    virtual ICrimeParticipant* FindCrimeParticipant(const cS3DVector3& position, float radius) = 0;   // spCrimeSim vslot 0x50
    virtual cIGZUnknown* NewRiotPolice(cSC4PatrolCar& car) = 0;     // new cSC4RiotPolice(car), reference count zero
    virtual void SetupRiotPolice(cIGZUnknown* riot, float competence) = 0;   // SetCompetence, spCity vslot 0x160 -> 0x38(add), SetupRiotPolice
    virtual void RemoveRiotPolice(cIGZUnknown* riot) = 0;           // spCity vslot 0x160 -> vslot 0x40(riot, 0)
    // cSC4Automaton::ReplaceModelInstance(first model, key) followed by the new instance's vslot 0x118(1)
    virtual void ReplaceArrestModel(cSC4PatrolCar& car, const cGZPersistResourceKey& key) = 0;
};

class cSC4PatrolCar : public cSC4EmergencyVehicle {
public:
    explicit cSC4PatrolCar(ITrafficAutomatonHost* host = nullptr, IAutomatonHost* automatonHost = nullptr,
                           IVehicleHost* vehicleHost = nullptr, IEmergencyVehicleHost* emergencyHost = nullptr,
                           IPatrolCarHost* patrolHost = nullptr, const cGZPersistResourceKey& modelKey = {});   // @ 0x10050e98c
    ~cSC4PatrolCar() override;                                                    // @ 0x10050f2e6

    bool Shutdown() override;                                                     // 0x20  @ 0x10050ea04
    // HandleReachedRouteDestination (0x2b0 @ 0x10050eaa6) and CloseEnoughToDispatchDestination (0x4c8 @ 0x10050eb14) only forward to the
    // emergency vehicle's, so the inherited ones are the same code.
    void SetDispatchState(std::uint32_t state) override;                          // 0xc8  @ 0x10050eb46
    bool Write(cIGZOStream& stream) const override;                               // 0x240 @ 0x10050ee38
    bool Read(cIGZIStream& stream) override;                                      // 0x248 @ 0x10050eeb0
    bool HandleEnterState(tAutomataState* state) override;                        // 0x3a8 @ 0x10050ef76
    bool DoStateTick(float milliseconds) override;                                // 0x3a0 @ 0x10050efba
    bool HandleExitState(const tAutomataState* state) override;                   // 0x3b0 @ 0x10050f28a
    bool DoHandlingEventTick(float milliseconds) override;                        // 0x660 @ 0x10050edc8
    std::uint32_t GetGZCLSID() const override { return kPatrolCarGZCLSID; }       // 0x250 @ 0x10050f544
    std::uint32_t GetDispatchType() const override { return kPatrolCarDispatchType; }   // 0x638 @ 0x10050f5ac
    float GetEffectiveRadius() const override;                                    // 0x640 @ 0x10050eab0

    virtual void HandleCrewFinished();                                            // 0x670 @ 0x10050eb1e
    virtual bool DoArrestTick(float milliseconds);                                // 0x678 @ 0x10050f00e
    void ReleaseRiotPolice();                                                     // @ 0x10050ea20
    void CreateRiotPolice();                                                      // @ 0x10050ec86

    cIGZUnknown* RiotPolice() const { return riot_; }
    IPatrolCarHost* PatrolHost() const { return patrolHost_; }

private:
    IPatrolCarHost* patrolHost_ = nullptr;      // port seam (not in the original)
    cIGZUnknown* riot_ = nullptr;               // +0x450 counted
};

}  // namespace sc4
