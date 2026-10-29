// cSC4Airplane — the aircraft automaton of the automata system (fire planes, police planes, water bombers and the
// player-driven plane). 62 symbols at 0x1006ccd2c..0x1006d5620, of which 17 are non-virtual thunks; class id
// 0xcbeb4f62. It derives from cSC4Aircraft (Aircraft.h) and adds the flight model: a state machine (flying / landing /
// taking off / taxiing), the force and orientation integration, the AI helpers that steer towards a runway, the
// landing/take-off/taxi sequences, the engine/wing/skywriting effects and the dispatch identity (call number + state).
//
// Original layout, relative to cSC4Aircraft (which ends at +0x308):
//   +0x310 State                +0x314 float control[4] (AI_Stop's eased controls; +0x320 also read by UpdateEffects)
//   +0x324 float gear/target A  +0x328 float gear/target B
//   +0x330 IAirplaneEffect* wing prop (still)   +0x338 IAirplaneEffect* wing prop (running)
//   +0x340 IAirplaneEffect* aircraft indicator  +0x348 IAirplaneEffect* waterdrop/skywriting
//   +0x350 cS3DVector3 skywriting registration point   +0x35c int skywriting colour index (0..5)
//   +0x360 bool skywriting active   +0x364 float skywriting timer   +0x368 int state counter (3 when landing)
//   +0x36c cS3DVector3 velocity   +0x378 roll   +0x37c pitch   +0x380 yaw (SetOrientation writes it)
//   +0x384 mass   +0x388 drag area   +0x38c low speed limit   +0x390 high speed limit
//   +0x394..+0x3a4 climb/descent/turn scaling   +0x3ac..+0x3bc angles   +0x3c4 climb slope   +0x3c8 descent slope
//   +0x3cc turn rate   +0x3d0..+0x3dc approach tuning   +0x3e0/+0x3e4 reset by HandleEnterState/ExitState
//   +0x3e8/+0x3ec/+0x3f0       +0x3f4 cS3DVector3[4] approach/runway points   +0x424 int
//   +0x428/+0x42c/+0x430 floats   +0x434 int   +0x438 float[12] player-drive tuning
//   +0x468 u16 call number   +0x46c dispatch state (cISC4DispatchManager::tDispatchState)
//   +0x470 cS3DVector3 destination copy for state 0x39   +0x480 cSC4ActiveFireOccupantFilter*
//   +0x488 bool "position was loaded"   +0x48c cS3DVector3 saved position
//
// Quirks kept:
//  * The ctor initialises the state counter (+0x368) to 3 even though the state is flying (0).
//  * Init computes the climb/descent slopes and the turn rate while the state is forced to 3 and restores the heading
//    around the computation; it also swaps the position with the saved position according to +0x488.
//  * When the plane belongs to occupant group 0x4311 Init first looks up the "fx_00_skywriting" registered effect,
//    reads the skywriting registration point from it, then always replaces it with the effects manager's
//    "skywritingwhite"; the looked-up effect therefore only has a side effect (its registration point).
//  * HandlePlayerDriveCommand(9) releases the waterdrop effect and creates the next skywriting colour; the colour
//    counter wraps through 0 after 5 (white only appears again through the wrap).
//  * HandlePlayerDriveCommand(7) on group 0x4303 restarts the 2 s skywriting timer and decrements the landing state
//    counter without ever going below 0.
//  * DoPlayerDriveTick divides the distance moved by the *unclamped* delta (0x1006cf77c).
//  * SetDispatchState sends callback event 7; DoMessage always returns true.
//
// The flight model, the AI helpers and the player-drive translation are transcribed from the disassembly; the float
// code is bit-identical to the retail code in scripts/run_machine_oracle.sh airplane (AI_PursueGoal, a dispatcher over
// the automaton's own state stack, is checked against the disassembly and the unit test).
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"
#include "sc4/Aircraft.h"
#include "sc4/OccupantManager.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4Airplane = 0xcbeb4f62;

// Occupant groups that change the airplane's special behaviour.
constexpr std::uint32_t kAirplaneGroupFire = 0x4311;   // water bomber / fire plane (skywriting + waterdrop)
constexpr std::uint32_t kAirplaneGroupPolice = 0x4303; // police plane (skywriting timer finish)
constexpr std::uint32_t kAirplaneStatePlayerDrive = 0x10;
constexpr std::uint32_t kAirplaneStateChase = 0x39;    // state whose destination is copied to +0x470

// The six "skywriting" effect names, initialised by __GLOBAL__sub_I_SC4Airplane.cpp at 0x100b8fa40..0x100b8fb08.
extern const char* const kSkywritingEffectNames[6];

// The airplane's state machine (+0x310). The names are descriptive; the numbers are the ones the binary compares.
enum class AirplaneState : std::int32_t {
    kFlying = 0,    // cruise / pursue the goal
    kLanding = 1,   // touchdown / final approach
    kTakingOff = 2, // rolling / climbing out
    kTaxiing = 3,   // on the ground
};

// cSC4Airplane::Controls (0x18 bytes): the control inputs handed to CalculateForces and UpdateOrientation. UpdatePhysics
// passes this+0x314 (the eased controls, +0x320 the throttle the propeller effect reads, +0x324/+0x328 the targets).
struct AirplaneControls {
    float steer[3] = {0.0f, 0.0f, 0.0f};   // +0x00..+0x08 (read by UpdateOrientation)
    float throttle = 0.0f;                 // +0x0c: thrust = +0x3b8 * throttle
    float lift = 0.0f;                     // +0x10: lift scale 1 + lift * +0x3bc
    float value14 = 0.0f;                  // +0x14
};

class cSC4Airplane final : public cSC4Aircraft {
public:
    explicit cSC4Airplane(IAutomatonHost* host = nullptr, IAircraftHost* aircraftHost = nullptr);
    ~cSC4Airplane() override;

    // ---- cSC4Aircraft / cSC4Automaton overrides ----------------------------------------------------------------
    bool Init() override;                                             // @ 0x1006ccf30
    bool Shutdown() override;                                         // @ 0x1006ce36e
    bool Write(cIGZOStream& stream) const override;                   // @ 0x1006ce480
    bool Read(cIGZIStream& stream) override;                          // @ 0x1006ce51e
    void SetOrientation(const float* direction);                      // @ 0x1006ce5d6 (vslot 0x70)
    bool DoMessage(cIGZMessage2* message) override;                   // @ 0x1006ce7e4
    bool DoAnimationTick(std::uint32_t data, float milliseconds) override;   // @ 0x1006ce7f4 (vslot 0x278)
    bool DoPlayerDriveTick(float milliseconds);                       // @ 0x1006cf77c (vslot 0x3d0)
    bool HandleEnterState(tAutomataState* state) override;            // @ 0x1006d2160 (vslot 0x3a8)
    bool HandleExitState(const tAutomataState* state) override;       // @ 0x1006d224a (vslot 0x3b0)
    bool HandlePlayerDriveCommand(std::uint32_t command, bool on);    // @ 0x1006d22a6 (vslot 0x1e0)
    void UpdateEffects();                                             // @ 0x1006d1e62 (vslot 0x378)
    void SetDispatchState(std::uint32_t state);                       // @ 0x1006d54c6 (vslot 0xc8)
    std::uint32_t GetDispatchState() const;                           // @ 0x1006d5514 (vslot 0xd0)
    std::uint16_t GetCallNumber() const;                              // @ 0x1006d54f8 (vslot 0xb8)
    void SetCallNumber(std::uint16_t callNumber);                     // @ 0x1006d5506 (vslot 0xc0)
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4Airplane; }   // @ 0x1006d5566

    // ---- drive parameters (group template blocks) ---------------------------------------------------------------
    void LoadPlayerDriveParameters();                                 // @ 0x1006cd5dc
    void LoadDriveParameters();                                       // @ 0x1006cd84a

    // ---- flight model ---------------------------------------------------------------------------------------------
    float CalculateClimbSlope();                                      // @ 0x1006cdbb2
    float CalculateDescentSlope();                                    // @ 0x1006cde04
    float CalculateTurnRate();                                        // @ 0x1006ce05e
    cS3DVector3 CalculateForces(const cS3DVector3& velocity, const cS3DVector3& forward,
                                const AirplaneControls& controls) const;   // @ 0x1006cff10
    void UpdateOrientation(const cS3DVector3& direction, cS3DVector3& heading, const AirplaneControls& controls,
                           cS3DTransform& transform, float delta);    // @ 0x1006d14e8
    void UpdatePhysics(float delta);                                  // @ 0x1006cee14
    void DetectCollisions();                                          // @ 0x1006d0cd0
    void SetAirplaneState(AirplaneState state);                       // @ 0x1006d0be2
    bool AI_TurnToVector(const cS3DVector3& vector, float delta);     // @ 0x1006d4852
    void AI_PursueGoal(float delta);                                  // @ 0x1006ce990
    bool AI_GotoDestination(const cS3DVector3& destination, float delta);   // @ 0x1006d2f12
    bool AI_Land(float delta);                                        // @ 0x1006d33f8
    bool AI_Stop(float delta);                                        // @ 0x1006d3f70
    bool AI_TakeOff(float delta);                                     // @ 0x1006d40e2
    void AI_Taxi(const cS3DVector3& target, float delta);             // @ 0x1006d44cc
    int AI_ApproachAltitude(float altitude, bool useLift, float delta);   // @ 0x1006d4a0c: 1 climb, 0 hold, -1 descend
    bool AI_FollowVectorAndTurn(const cS3DVector3& inbound, const cS3DVector3& turnPoint,
                                const cS3DVector3& outbound, float delta);   // @ 0x1006d4df8
    bool AI_FollowRay(const cS3DVector3& origin, const cS3DVector3& direction, float delta);   // @ 0x1006d51a2
    void PickRunwayDirection(const cS3DVector3& position, cS3DVector3& start, cS3DVector3& direction,
                             bool landing);                       // @ 0x1006d258c
    void PickApproachBox(const cS3DVector3& runwayEnd, const cS3DVector3& direction);   // @ 0x1006d2932
    bool TranslatePlayerDriveCommands(tAutomataStatePlayerDrive* state, float milliseconds);   // @ 0x1006cf894
    float EvaluateClearance(const cS3DVector3& start, const cS3DVector3& direction) const;      // @ 0x1006d2454
    bool FindFireNearLocation(const cS3DVector3& location, cS3DVector3& fire) const;            // @ 0x1006d2db0

    // ---- accessors for the neighbours and the test ---------------------------------------------------------------
    AirplaneState State() const { return state_; }
    int StateCounter() const { return stateCounter_; }
    const cS3DVector3& Velocity() const { return velocity_; }
    float Roll() const { return roll_; }
    float Pitch() const { return pitch_; }
    float Yaw() const { return yaw_; }
    const cS3DVector3& RegPoint() const { return regPoint_; }
    int SkywritingColor() const { return skywritingColor_; }
    bool SkywritingActive() const { return skywritingActive_; }
    float SkywritingTimer() const { return skywritingTimer_; }
    IAirplaneEffect* EffectPlanepropStill() const { return effectPlanepropStill_; }
    IAirplaneEffect* EffectPlaneprop() const { return effectPlaneprop_; }
    IAirplaneEffect* EffectIndicate() const { return effectIndicate_; }
    IAirplaneEffect* EffectDrop() const { return effectDrop_; }
    cSC4OccupantFilter* FireFilter() const { return fireFilter_; }
    bool PositionLoaded() const { return loaded_; }
    const cS3DVector3& SavedPosition() const { return savedPosition_; }
    const cS3DVector3& DestinationCopy() const { return destinationCopy_; }
    const cS3DVector3& ApproachPoint(int i) const { return points_[i]; }   // +0x3f4 + 12 * i
    cS3DVector3 RunwayTarget() const { return {field428_, field42c_, field430_}; }   // +0x428
    float Tuning(int index) const { return tunings_[index]; }
    float Mass() const { return mass_; }
    float ClimbSlope() const { return climbSlope_; }
    float DescentSlope() const { return descentSlope_; }
    float TurnRate() const { return turnRate_; }
    float MaxSpeedLow() const { return maxSpeedLow_; }
    float MaxSpeedHigh() const { return maxSpeedHigh_; }
    float Field320() const { return controlWish_[3]; }
    const float* ControlWish() const { return controlWish_; }

private:
    friend struct AirplaneOracleAccess;   // scripts/machine_oracle/airplane_oracle.cpp sets the flight fields

    // one 0.1 s step of the trial flight the slope and turn-rate calculations run; true once `settled` says so
    bool TrialFlightStep(cS3DVector3& velocity, cS3DVector3& forward, const AirplaneControls& controls);

    AirplaneState state_ = AirplaneState::kFlying;   // +0x310
    float controlWish_[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // +0x314..+0x320 (AI_Stop's eased controls)
    float controlAim_ = 0.0f;                        // +0x324
    float controlAim2_ = 0.0f;                       // +0x328
    IAirplaneEffect* effectPlanepropStill_ = nullptr;   // +0x330
    IAirplaneEffect* effectPlaneprop_ = nullptr;        // +0x338
    IAirplaneEffect* effectIndicate_ = nullptr;         // +0x340
    IAirplaneEffect* effectDrop_ = nullptr;             // +0x348
    cS3DVector3 regPoint_;                           // +0x350
    int skywritingColor_ = 0;                        // +0x35c
    bool skywritingActive_ = false;                  // +0x360
    float skywritingTimer_ = 0.0f;                   // +0x364
    int stateCounter_ = 3;                           // +0x368
    cS3DVector3 velocity_;                           // +0x36c
    float roll_ = 0.0f;                              // +0x378
    float pitch_ = 0.0f;                             // +0x37c
    float yaw_ = 0.0f;                               // +0x380
    float mass_ = 100.0f;                            // +0x384
    float dragArea_ = 1.0f;                          // +0x388
    float maxSpeedLow_ = 25.0f;                      // +0x38c
    float maxSpeedHigh_ = 150.0f;                    // +0x390
    float climbScale_ = 0.06544984f;                 // +0x394
    float descentScale_ = 0.3926991f;                // +0x398
    float turnScale_ = 0.04363323f;                  // +0x39c
    float field3a0_ = 10000.0f;                      // +0x3a0
    float field3a4_ = 0.04f;                         // +0x3a4
    float climbAngle_ = 0.2617994f;                  // +0x3ac (15 degrees)
    float descentAngle_ = 0.7853982f;                // +0x3b0 (45 degrees)
    float takeoffAngle_ = 0.5235988f;                // +0x3b4 (30 degrees)
    float field3b8_ = 10000.0f;                      // +0x3b8
    float field3bc_ = 0.4f;                          // +0x3bc
    float climbSlope_ = 0.0f;                        // +0x3c4
    float descentSlope_ = 0.0f;                      // +0x3c8
    float turnRate_ = 0.0f;                          // +0x3cc
    float approachAngle_ = 0.5235988f;               // +0x3d0
    float approachScale_ = 0.08f;                    // +0x3d4
    float approachFactor_ = 0.68f;                   // +0x3d8
    float field3dc_ = 0.0f;                          // +0x3dc
    int takeOffStep_ = 0;                              // +0x3e0
    int field3e4_ = 0;                                 // +0x3e4
    float field3e8_ = 0.0f;                          // +0x3e8
    float field3ec_ = 0.0f;                          // +0x3ec
    float field3f0_ = 0.0f;                          // +0x3f0
    cS3DVector3 points_[4];                          // +0x3f4..+0x423
    int field424_ = 0;                               // +0x424
    float field428_ = 0.0f;                          // +0x428
    float field42c_ = 0.0f;                          // +0x42c
    float field430_ = 0.0f;                          // +0x430
    int field434_ = 0;                               // +0x434
    float tunings_[12] = {0.4f, 0.3f, 0.2f, 1.0f, 0.5f, 0.2f, 0.1f, 0.3f, 0.1f, 0.2f, 0.2f, 0.1f};   // +0x438
    std::uint16_t callNumber_ = 0;                   // +0x468
    std::uint32_t dispatchState_ = 0;                // +0x46c
    cS3DVector3 destinationCopy_;                    // +0x470
    cSC4OccupantFilter* fireFilter_ = nullptr;       // +0x480
    bool loaded_ = false;                            // +0x488
    cS3DVector3 savedPosition_;                      // +0x48c
};

}  // namespace sc4
