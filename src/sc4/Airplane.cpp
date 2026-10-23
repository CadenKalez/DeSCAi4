// cSC4Airplane — see Airplane.h for the recovered layout, quirks and the PARTIAL list.
#include "sc4/Airplane.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "sc4/AutomataStates.h"
#include "sc4/OccupantFilters3.h"

namespace sc4 {

// __GLOBAL__sub_I_SC4Airplane.cpp @ 0x1006d5c2f..0x1006d5dd6.
const char* const kSkywritingEffectNames[6] = {
    "skywritingwhite", "skywritingyellow", "skywritinggreen",
    "skywritingblue",  "skywritingpurple", "skywritingred",
};

namespace {

// The kUninit3DVector sentinel (bits 0xffffffff / 0x01ffffff / 0x4f5f4090, see TrafficAutomaton.h).
cS3DVector3 UninitVector() {
    const std::uint32_t bits[3] = {0xffffffffu, 0x01ffffffu, 0x4f5f4090u};
    cS3DVector3 out;
    std::memcpy(&out.x, &bits[0], 4);
    std::memcpy(&out.y, &bits[1], 4);
    std::memcpy(&out.z, &bits[2], 4);
    return out;
}

IAircraftHost* HostFor(const cSC4Aircraft& aircraft) {
    IAircraftHost* host = aircraft.AircraftHostPtr();
    return host ? host : AircraftHost();
}

// cSC4Automaton::FindRegEffect (vslot 0x150) returns the effect object through the effects collection.
IAirplaneEffect* LookupRegEffect(cSC4Airplane& airplane, const char* name) {
    IAutomatonEffects* effects = airplane.Effects();
    if (!effects) return nullptr;
    return static_cast<IAirplaneEffect*>(effects->FindRegEffect(cRZString(name)));
}

void AssignEffect(IAirplaneEffect*& slot, IAirplaneEffect* value) {
    if (slot == value) return;
    if (value) value->AddRef();
    IAirplaneEffect* old = slot;
    slot = value;
    if (old) old->Release();
}

float Clamp(float value, float limit) {
    // The original's mask idiom: value < -limit ? -limit : min(value, limit) (NaN keeps the positive limit).
    float result = (value <= limit) ? value : limit;
    if (value < -limit) result = -limit;
    return result;
}

}  // namespace

cSC4Airplane::cSC4Airplane(IAutomatonHost* host, IAircraftHost* aircraftHost) : cSC4Aircraft(host, aircraftHost) {
    state_ = AirplaneState::kFlying;                       // +0x310
    controlWish_[0] = controlWish_[1] = controlWish_[2] = controlWish_[3] = 0.0f;   // +0x314..+0x320
    controlAim_ = 0.0f;                                    // +0x324
    controlAim2_ = 0.0f;                                   // +0x328
    effectPlanepropStill_ = nullptr;                       // +0x330
    effectPlaneprop_ = nullptr;                            // +0x338
    effectIndicate_ = nullptr;                             // +0x340
    effectDrop_ = nullptr;                                 // +0x348
    regPoint_ = cS3DVector3{0.0f, 0.0f, 0.0f};             // +0x350
    skywritingColor_ = 0;                                  // +0x35c
    skywritingActive_ = false;                             // +0x360
    skywritingTimer_ = 0.0f;                               // +0x364
    stateCounter_ = 3;                                     // +0x368
    velocity_ = cS3DVector3{0.0f, 0.0f, 0.0f};             // +0x36c
    roll_ = pitch_ = yaw_ = 0.0f;                          // +0x378/+0x37c/+0x380
    field3dc_ = 0.0f;                                      // +0x3dc
    field3e0_ = field3e4_ = 0.0f;                          // +0x3e0
    field3e8_ = field3ec_ = field3f0_ = 0.0f;              // +0x3e8
    for (cS3DVector3& point : points_) point = cS3DVector3{0.0f, 0.0f, 0.0f};   // +0x3f4..+0x423
    field424_ = 0;                                         // +0x424
    field428_ = field42c_ = field430_ = 0.0f;              // +0x428
    field434_ = 0;                                         // +0x434
    const float tunings[12] = {0.4f, 0.3f, 0.2f, 1.0f, 0.5f, 0.2f, 0.1f, 0.3f, 0.1f, 0.2f, 0.2f, 0.1f};
    std::memcpy(tunings_, tunings, sizeof(tunings_));      // +0x438..+0x464
    callNumber_ = 0;                                       // +0x468
    dispatchState_ = 0;                                    // +0x46c
    destinationCopy_ = UninitVector();                     // +0x470
    fireFilter_ = nullptr;                                 // +0x480
    loaded_ = false;                                       // +0x488
    savedPosition_ = cS3DVector3{0.0f, 0.0f, 0.0f};        // +0x48c
    orientation_ = kAircraftModelFacing;                   // +0x8c = kModelFacing
}

cSC4Airplane::~cSC4Airplane() {
    if (fireFilter_) { fireFilter_->Release(); fireFilter_ = nullptr; }
    if (effectDrop_) { effectDrop_->Release(); effectDrop_ = nullptr; }
    if (effectIndicate_) { effectIndicate_->Release(); effectIndicate_ = nullptr; }
    if (effectPlaneprop_) { effectPlaneprop_->Release(); effectPlaneprop_ = nullptr; }
    if (effectPlanepropStill_) { effectPlanepropStill_->Release(); effectPlanepropStill_ = nullptr; }
}

// @ 0x1006ccf30.
bool cSC4Airplane::Init() {
    if (IsInitialized()) return true;
    cSC4Aircraft::Init();
    StopNamedEffects(cRZString("jetbomb"), 1);
    LoadPlayerDriveParameters();
    LoadDriveParameters();
    state_ = AirplaneState::kTaxiing;   // 3: the slope computation runs at taxi speed
    const cS3DVector3 savedOrientation = orientation_;
    const float facing[3] = {kAircraftModelFacing.x, kAircraftModelFacing.y, kAircraftModelFacing.z};
    SetOrientation(facing);
    climbSlope_ = CalculateClimbSlope();      // +0x3c4 (PARTIAL helper)
    descentSlope_ = CalculateDescentSlope();  // +0x3c8
    turnRate_ = CalculateTurnRate();          // +0x3cc
    state_ = AirplaneState::kFlying;          // 0
    SetOrientation(&savedOrientation.x);
    // +0x488 selects between storing the current position and restoring the saved one.
    if (!loaded_) {
        savedPosition_ = position_;
    } else {
        position_ = savedPosition_;
    }
    // Wing prop effects: "fx_00_planepropstill"/"fx_00_fireplanepropstill", then "fx_00_planeprop"/"fx_00_fireplaneprop".
    IAirplaneEffect* effect = LookupRegEffect(*this, "fx_00_planepropstill");
    if (!effect) effect = LookupRegEffect(*this, "fx_00_fireplanepropstill");
    AssignEffect(effectPlanepropStill_, effect);
    effect = LookupRegEffect(*this, "fx_00_planeprop");
    if (!effect) effect = LookupRegEffect(*this, "fx_00_fireplaneprop");
    AssignEffect(effectPlaneprop_, effect);
    regPoint_ = cS3DVector3{0.0f, 0.0f, 0.0f};
    IAircraftHost* host = HostFor(*this);
    if (!IsOccupantGroup(kAirplaneGroupFire)) {
        AssignEffect(effectDrop_, LookupRegEffect(*this, "fx_00_waterdrop"));
    } else {
        AssignEffect(effectDrop_, LookupRegEffect(*this, "fx_00_skywriting"));
        // Quirk: the registration point is read from the found effect, then the effect is always replaced by the
        // effects manager's "skywritingwhite".
        cS3DVector3 point{0.0f, 0.0f, 0.0f};
        if (host && host->FindRegPointTransform(cRZString("fx_00_skywriting"), point)) regPoint_ = point;
        skywritingColor_ = 0;   // +0x35c
        if (effectDrop_) { effectDrop_->Release(); effectDrop_ = nullptr; }
        if (host) effectDrop_ = host->CreateEffect(cRZString("skywritingwhite"));
    }
    if (effectIndicate_) { effectIndicate_->Release(); effectIndicate_ = nullptr; }
    if (host) effectIndicate_ = host->CreateEffect(cRZString("aircraftindicate"));
    // The occupant filter used by FindFireNearLocation (cSC4ActiveFireOccupantFilter).
    auto* filter = new cSC4ActiveFireOccupantFilter();
    if (filter != fireFilter_) {
        filter->AddRef();
        cSC4OccupantFilter* old = fireFilter_;
        fireFilter_ = filter;
        if (old) old->Release();
    }
    return true;
}

// @ 0x1006ce36e.
bool cSC4Airplane::Shutdown() {
    if (fireFilter_) { fireFilter_->Release(); fireFilter_ = nullptr; }
    IAirplaneEffect* effects[4] = {effectPlaneprop_, effectPlanepropStill_, effectDrop_, effectIndicate_};
    IAirplaneEffect** slots[4] = {&effectPlaneprop_, &effectPlanepropStill_, &effectDrop_, &effectIndicate_};
    for (int i = 0; i < 4; ++i) {
        if (effects[i]) {
            effects[i]->Stop(true);
            *slots[i] = nullptr;
            effects[i]->Release();
        }
    }
    return cSC4Aircraft::Shutdown();
}

// @ 0x1006ce480. Stream version 1, the aircraft record, then the saved position; returns !stream error.
bool cSC4Airplane::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    if (cSC4Aircraft::Write(stream)) {
        stream.SetFloat32(savedPosition_.x);
        stream.SetFloat32(savedPosition_.y);
        stream.SetFloat32(savedPosition_.z);
    }
    return !stream.GetError();
}

// @ 0x1006ce51e. Versions 0 and 1 are accepted (the original compares `< 2`).
bool cSC4Airplane::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version >= 2) return false;
    if (cSC4Aircraft::Read(stream)) {
        stream.GetFloat32(savedPosition_.x);
        stream.GetFloat32(savedPosition_.y);
        stream.GetFloat32(savedPosition_.z);
        loaded_ = true;
    }
    return !stream.GetError();
}

// @ 0x1006ce5d6. The base stores the unit heading; this also derives the yaw angle (+0x380) between the new heading and
// the model facing (+Z), with the original's antiparallel and aligned special cases.
void cSC4Airplane::SetOrientation(const float* direction) {
    cSC4Automaton::SetOrientation(direction);
    const float dx = kAircraftModelFacing.x;
    const float dy = kAircraftModelFacing.y;
    const float dz = kAircraftModelFacing.z;
    const float ox = orientation_.x;
    const float oy = orientation_.y;
    const float oz = orientation_.z;
    const float dot = dz * oz + dy * oy + dx * ox;
    float cosHalf = 1.0f;
    float sinHalf = 0.0f;
    if (dot < 0.9999f) {
        if (dot <= -0.9999f) {
            const float facingSquared = dy * dy + dx * dx;
            float a = dy;
            float b = -dz;
            float c = dx;
            if (facingSquared >= 1e-8f) {
                a = -dy;
                c = 0.0f;
                b = 0.0f;
            }
            cosHalf = 0.0f;
            sinHalf = (1.0f / std::sqrt(a * a + b * b + c * c)) * c;
        } else {
            const float crossX = dy * oz - oy * dz;
            const float crossY = dz * ox - oz * dx;
            const float crossZ = dx * oy - ox * dy;
            const float length = std::sqrt(crossX * crossX + crossY * crossY + crossZ * crossZ);
            const float half = std::sqrt((1.0f - dot) * 0.5f);
            sinHalf = half * ((1.0f / length) * crossY);
            cosHalf = std::sqrt((dot + 1.0f) * 0.5f);
        }
    }
    const float sine = std::sqrt(1.0f - cosHalf * cosHalf);
    const float angle = 2.0f * std::acos(cosHalf);
    const float factor = (std::fabs(sine) < 0.0001f) ? 1.0f : 1.0f / sine;
    yaw_ = (0.0f < factor * sinHalf) ? -angle : angle;   // +0x380
}

// @ 0x1006ce7e4. Empty in the shipping binary.
bool cSC4Airplane::DoMessage(cIGZMessage2*) { return true; }

// @ 0x1006ce7f4.
bool cSC4Airplane::DoAnimationTick(std::uint32_t data, float milliseconds) {
    IAircraftHost* host = HostFor(*this);
    float delta = milliseconds * 0.001f;
    if (!IsPlayerDriving()) {
        float multiplier = 1.0f;
        if (host) multiplier = host->DisasterActive() ? host->DisasterSpeedMultiplier()
                                                      : (Host() ? Host()->SimSpeedModifier() : 1.0f);
        delta *= multiplier;
    }
    const float step = (delta <= 0.2f) ? delta : 0.2f;
    if (0.0f < skywritingTimer_) skywritingTimer_ -= step;   // +0x364
    UpdateOpacity(milliseconds);
    UpdateAnimation(milliseconds);
    DoStateTick(milliseconds);
    const bool activeSound = (0.0f < controlWish_[3]) && state_ != AirplaneState::kLanding;
    EnableActiveSound(activeSound);
    UpdateActiveSound(milliseconds);
    if (GetBehavior() != kAirplaneStatePlayerDrive) {
        AI_PursueGoal(step);    // PARTIAL helper
        UpdatePhysics(step);    // PARTIAL helper
        UpdateLighting();
        UpdateEffects();
        FinishMove(true, true);
    }
    if (skywritingActive_ && skywritingTimer_ <= 0.0f) {
        if (IsOccupantGroup(kAirplaneGroupPolice)) skywritingActive_ = false;
    }
    UpdateGhostModel();
    (void)data;
    return true;
}

// @ 0x1006cf77c. The vehicle-family player-drive tick followed by one physics step.
bool cSC4Airplane::DoPlayerDriveTick(float milliseconds) {
    const bool result = cSC4Automaton::DoPlayerDriveTick(milliseconds);
    float step = milliseconds * 0.001f;
    if (step > 0.2f) step = 0.2f;
    const cS3DVector3 start = position_;
    UpdatePhysics(step);
    UpdateLighting();
    UpdateEffects();
    FinishMove(true, true);
    const float dx = position_.x - start.x;
    const float dy = position_.y - start.y;
    const float dz = position_.z - start.z;
    actualSpeed_ = std::sqrt(dx * dx + dy * dy + dz * dz) / milliseconds;   // +0xc8
    tAutomataState* state = GetState();
    if (state) {
        IAircraftHost* host = HostFor(*this);
        if (host) {
            // The original writes the cell pair into the state record's +0x298/+0x29c; the port only reports it.
            std::int32_t cellX = 0, cellZ = 0;
            host->WorldToCell(position_.x, position_.z, cellX, cellZ);
        }
    }
    return result;
}

// @ 0x1006cd5dc. Reads the player-drive block (+0xe8) of the automata group template; zero fields keep the current value.
void cSC4Airplane::LoadPlayerDriveParameters() {
    IAircraftHost* host = HostFor(*this);
    if (!host) return;
    const float* values = host->FindPlayerDriveParameters(GetAutomataGroup());
    if (!values) return;
    if (values[2] != 0.0f) maxSpeedHigh_ = values[2] * 1000.0f;             // +0x390
    if (values[0x18] != 0.0f) tunings_[0] = values[0x18] * 0.01f;           // +0x438 (0x60/4)
    if (values[0x1e] != 0.0f) tunings_[1] = values[0x1e] * 0.01f;           // +0x43c (0x78/4)
    if (values[0x19] != 0.0f) tunings_[2] = values[0x19] * 0.01f;           // +0x440 (100/4)
    if (values[0x1a] != 0.0f) tunings_[3] = values[0x1a] * 0.01f;           // +0x444 (0x68/4)
    if (values[0x1b] != 0.0f) tunings_[4] = values[0x1b] * 0.01f;           // +0x448 (0x6c/4)
    if (values[0x1f] != 0.0f) tunings_[5] = values[0x1f] * 0.01f;           // +0x44c (0x7c/4)
    if (values[0x20] != 0.0f) tunings_[6] = values[0x20] * 0.01f;           // +0x450 (0x80/4)
    if (values[0x21] != 0.0f) tunings_[7] = values[0x21] * 0.01f;           // +0x454 (0x84/4)
    if (values[0x22] != 0.0f) tunings_[8] = values[0x22] * 0.01f;           // +0x458 (0x88/4)
    if (values[0x24] != 0.0f) tunings_[9] = values[0x24] * 0.01f;           // +0x45c (0x90/4)
    if (values[0x1c] != 0.0f) tunings_[10] = values[0x1c] * 0.01f;          // +0x460 (0x70/4)
    if (values[0x23] != 0.0f) tunings_[11] = values[0x23] * 0.01f;          // +0x464 (0x8c/4)
}

// @ 0x1006cd84a. Reads the 18-float drive block (+0xe0) of the automata group template; zero entries keep the default.
void cSC4Airplane::LoadDriveParameters() {
    IAircraftHost* host = HostFor(*this);
    if (!host) return;
    const float* values = host->FindDriveParameters(GetAutomataGroup());
    if (!values) {
        // Defaults of the shipping binary.
        mass_ = 100.0f;            // +0x384
        dragArea_ = 1.0f;          // +0x388
        maxSpeedLow_ = 25.0f;      // +0x38c
        maxSpeedHigh_ = 150.0f;    // +0x390
        climbAngle_ = 0.2617994f;  // +0x3ac (15 deg)
        descentAngle_ = 0.7853982f;   // +0x3b0 (45 deg)
        takeoffAngle_ = 0.5235988f;   // +0x3b4 (30 deg)
        field3b8_ = 10000.0f;      // +0x3b8
        field3bc_ = 0.4f;          // +0x3bc
        climbScale_ = 0.06544984f; // +0x394
        descentScale_ = 0.3926991f;// +0x398
        turnScale_ = 0.04363323f;  // +0x39c
        field3a0_ = 10000.0f;      // +0x3a0
        field3a4_ = 0.04f;         // +0x3a4
        approachAngle_ = 0.5235988f;   // +0x3d0
        approachScale_ = 0.08f;    // +0x3d4
        approachFactor_ = 0.68f;   // +0x3d8
        field3dc_ = 0.0f;          // +0x3dc
        return;
    }
    if (values[0] != 0.0f) mass_ = values[0];                       // pf[0] -> +0x384 (default 100)
    if (values[1] != 0.0f) dragArea_ = values[1];                   // pf[1] -> +0x388 (default 1)
    if (values[2] * 1000.0f != 0.0f) maxSpeedLow_ = values[2] * 1000.0f;    // +0x38c (default 25)
    if (values[3] * 1000.0f != 0.0f) maxSpeedHigh_ = values[3] * 1000.0f;   // +0x390 (default 150)
    const float climb = (values[4] != 0.0f) ? values[4] : 0.2617994f;
    const float descent = (values[6] != 0.0f) ? values[6] : 0.7853982f;
    const float takeoff = (values[8] != 0.0f) ? values[8] : 0.5235988f;
    const float speed4 = (values[10] != 0.0f) ? values[10] : 10000.0f;
    const float drag = (values[12] != 0.0f) ? values[12] : 0.4f;
    climbAngle_ = climb;           // +0x3ac
    descentAngle_ = descent;       // +0x3b0
    takeoffAngle_ = takeoff;       // +0x3b4
    field3b8_ = speed4;            // +0x3b8
    field3bc_ = drag;              // +0x3bc
    climbScale_ = (values[5] != 0.0f) ? values[5] : climb * 0.25f;          // +0x394
    descentScale_ = (values[7] != 0.0f) ? values[7] : descent * 0.5f;       // +0x398
    turnScale_ = (values[9] != 0.0f) ? values[9] : takeoff / 12.0f;         // +0x39c
    field3a0_ = (values[0xb] != 0.0f) ? values[0xb] : speed4;               // +0x3a0
    field3a4_ = (values[0xd] != 0.0f) ? values[0xd] : drag / 10.0f;         // +0x3a4
    if (values[0xe] != 0.0f) approachAngle_ = values[0xe];                  // +0x3d0
    if (values[0xf] != 0.0f) approachScale_ = values[0xf];                  // +0x3d4
    if (values[0x10] != 0.0f) approachFactor_ = values[0x10];               // +0x3d8
    field3dc_ = (values[0x11] != 0.0f) ? values[0x11] : 0.0f;   // +0x3dc
}

// ---- PARTIAL flight model -------------------------------------------------------------------------------------------
// The force/velocity/quaternion core is not ported yet (ledger PARTIAL); the stand-ins keep the call graph honest.

// @ 0x1006cdbb2 PARTIAL: needs CalculateForces/UpdateOrientation (not ported).
float cSC4Airplane::CalculateClimbSlope() { return 0.0f; }

// @ 0x1006cde04 PARTIAL: needs CalculateForces/UpdateOrientation (not ported).
float cSC4Airplane::CalculateDescentSlope() { return 0.0f; }

// @ 0x1006ce05e PARTIAL: needs CalculateForces/UpdateOrientation (not ported).
float cSC4Airplane::CalculateTurnRate() { return 0.0f; }

// @ 0x1006cff10 PARTIAL: the 3.2 KB quaternion/lift/drag pipeline is not ported.
cS3DVector3 cSC4Airplane::CalculateForces(const cS3DVector3& orientation, const cS3DVector3& velocity,
                                          const AirplaneControls& controls) const {
    (void)orientation;
    (void)velocity;
    (void)controls;
    return cS3DVector3{0.0f, 0.0f, 0.0f};
}

// @ 0x1006d14e8 PARTIAL: the orientation quaternion integration is not ported; the identity transform is left alone.
void cSC4Airplane::UpdateOrientation(const cS3DVector3& direction, cS3DVector3& velocity,
                                     const AirplaneControls& controls, cS3DTransform& transform, float delta) {
    (void)direction;
    (void)velocity;
    (void)controls;
    (void)transform;
    (void)delta;
}

// @ 0x1006cee14 PARTIAL: the airport/runway-aware flight step is not ported.
void cSC4Airplane::UpdatePhysics(float delta) { (void)delta; }

// @ 0x1006d0cd0 PARTIAL: the collision sweep is not ported.
void cSC4Airplane::DetectCollisions() {}

// @ 0x1006ce990 PARTIAL: the goal selection (fire search / landing / take-off) is not ported.
void cSC4Airplane::AI_PursueGoal(float delta) { (void)delta; }

// @ 0x1006d2f12 PARTIAL: waypoint following is not ported.
bool cSC4Airplane::AI_GotoDestination(const cS3DVector3& destination, float delta) {
    (void)destination;
    (void)delta;
    return false;
}

// @ 0x1006d33f8 PARTIAL: the runway/approach landing sequence is not ported.
bool cSC4Airplane::AI_Land(float delta) {
    (void)delta;
    return false;
}

// @ 0x1006d3f70 PARTIAL: the SIMD control easing is not ported.
bool cSC4Airplane::AI_Stop(float delta) {
    (void)delta;
    return false;
}

// @ 0x1006d40e2 PARTIAL: the take-off sequence is not ported.
bool cSC4Airplane::AI_TakeOff(float delta) {
    (void)delta;
    return false;
}

// @ 0x1006d44cc PARTIAL: runway taxiing is not ported.
void cSC4Airplane::AI_Taxi(const cS3DVector3& target, float delta) {
    (void)target;
    (void)delta;
}

// @ 0x1006d4a0c PARTIAL: the altitude approach controller is not ported.
bool cSC4Airplane::AI_ApproachAltitude(float altitude, bool flag, float delta) {
    (void)altitude;
    (void)flag;
    (void)delta;
    return false;
}

// @ 0x1006d4df8 PARTIAL: the follow-vector controller is not ported.
bool cSC4Airplane::AI_FollowVectorAndTurn(const cS3DVector3& position, const cS3DVector3& direction,
                                          const cS3DVector3& up, float delta) {
    (void)position;
    (void)direction;
    (void)up;
    (void)delta;
    return false;
}

// @ 0x1006d51a2 PARTIAL: the ray follower is not ported.
bool cSC4Airplane::AI_FollowRay(const cS3DVector3& origin, const cS3DVector3& direction, float delta) {
    (void)origin;
    (void)direction;
    (void)delta;
    return false;
}

// @ 0x1006d258c PARTIAL: needs cSC4AircraftManager::GetRunwayList (unrecovered).
void cSC4Airplane::PickRunwayDirection(const cS3DVector3& position, cS3DVector3& start, cS3DVector3& end, bool flag) {
    (void)position;
    (void)start;
    (void)end;
    (void)flag;
}

// @ 0x1006d2932 PARTIAL: needs the runway list and cS4AircraftManager helpers (unrecovered).
void cSC4Airplane::PickApproachBox(const cS3DVector3& start, const cS3DVector3& end) {
    (void)start;
    (void)end;
}

// @ 0x1006cf894 PARTIAL: the player-drive control translation (1.6 KB, effect timers + tween) is not ported.
bool cSC4Airplane::TranslatePlayerDriveCommands(tAutomataStatePlayerDrive* state, float milliseconds) {
    (void)state;
    (void)milliseconds;
    return false;
}

// @ 0x1006d4852. Steer the 2D heading towards `vector`: ease +0x318 towards the cross product when the angle is over 2
// degrees, then ease +0x31c towards it (clamped to +/-0.2 while not landing/taking off); true when within ~6 degrees.
bool cSC4Airplane::AI_TurnToVector(const cS3DVector3& vector, float delta) {
    const float tx = vector.x;
    const float tz = vector.z;
    const float ox = orientation_.x;
    const float oz = orientation_.z;
    const float invTarget = 1.0f / std::sqrt(tz * tz + tx * tx + 0.0f);
    const float invOrientation = 1.0f / std::sqrt(oz * oz + ox * ox + 0.0f);
    const float nx = tx * invTarget;
    const float onx = ox * invOrientation;
    const float onz = oz * invOrientation;
    const float cross = invTarget * tz * onx - nx * invOrientation * oz;
    const float dot = invOrientation * 0.0f * invTarget * 0.0f + nx * onx + onz * invTarget * tz;
    float clampedDot = (dot <= 1.0f) ? dot : 1.0f;
    if (dot < -1.0f) clampedDot = -1.0f;
    const float angle = std::acos(clampedDot);
    if (0.034906585f < angle) {
        const float maxStep = tunings_[2] * delta;   // +0x440
        const float diff = cross - controlWish_[1];
        controlWish_[1] += Clamp(diff, maxStep);     // +0x318
    }
    float target = cross;
    if (state_ < AirplaneState::kTakingOff) {
        target = (cross <= 0.2f) ? cross : 0.2f;
        if (cross < -0.2f) target = -0.2f;
    }
    const float maxStep = tunings_[10] * delta;      // +0x460
    controlWish_[2] += Clamp(target - controlWish_[2], maxStep);   // +0x31c
    return angle < 0.10471976f;
}

// @ 0x1006d0be2.
void cSC4Airplane::SetAirplaneState(AirplaneState state) {
    const AirplaneState old = state_;
    if (old == state) return;
    IAircraftHost* host = HostFor(*this);
    bool done = false;
    if (state == AirplaneState::kLanding) {
        stateCounter_ = 3;   // +0x368
        if (old == AirplaneState::kFlying) {
            if (!IsPlayerDriving()) {
                done = true;
            } else {
                if (host) host->PlayAudioEvent(9);
            }
        }
        if (!done && (state_ != AirplaneState::kTaxiing || !IsPlayerDriving())) done = true;
        if (!done) {
            if (host) {
                host->StopAudioEvent(0xd);
                host->PlayAudioEvent(0xd);
            }
        }
    } else if (state == AirplaneState::kFlying && IsPlayerDriving()) {
        if (host) {
            host->StopAudioEvent(9);
            host->StopAudioEvent(8);
            host->StopAudioEvent(10);
            host->PlayAudioEvent(10);
        }
    }
    state_ = state;
}

// @ 0x1006d2160.
bool cSC4Airplane::HandleEnterState(tAutomataState* state) {
    const bool result = cSC4Aircraft::HandleEnterState(state);
    if (!state) {
        field3e0_ = field3e4_ = 0.0f;
        field434_ = 0;
        field424_ = 0;
    } else {
        if (state->stateType == kAirplaneStatePlayerDrive) {
            LoadPlayerDriveParameters();
            auto* drive = static_cast<tAutomataStatePlayerDrive*>(state);
            drive->value40 = 0;
            drive->groupSpeed = 1.0f;
            drive->value170 = 0xf;
            drive->animStage1a4 = 0;
            drive->timeout1d8 = 0x44fa0000;   // 2000.0f
            if (state_ != AirplaneState::kFlying) {
                if (IAircraftHost* host = HostFor(*this)) host->PlayAudioEvent(8);
            }
        } else {
            field3e0_ = field3e4_ = 0.0f;
            field434_ = 0;
            field424_ = 0;
        }
        if (state->stateType == kAirplaneStateChase) destinationCopy_ = destination_;
    }
    skywritingActive_ = false;   // +0x360
    return result;
}

// @ 0x1006d224a.
bool cSC4Airplane::HandleExitState(const tAutomataState* state) {
    const bool result = cSC4Aircraft::HandleExitState(state);
    if (state && state->stateType == kAirplaneStatePlayerDrive) {
        LoadDriveParameters();
        field3e0_ = field3e4_ = 0.0f;
        field434_ = 0;
        field424_ = 0;
    }
    skywritingActive_ = false;
    return result;
}

// @ 0x1006d22a6.
bool cSC4Airplane::HandlePlayerDriveCommand(std::uint32_t command, bool on) {
    bool result = cSC4Automaton::HandlePlayerDriveCommand(command, on);
    if (!on) return result;
    IAircraftHost* host = HostFor(*this);
    if (command == 9) {
        if (IsOccupantGroup(kAirplaneGroupFire)) {
            if (effectDrop_) {
                effectDrop_->Stop(false);
                effectDrop_->Release();
                effectDrop_ = nullptr;
            }
            int index = skywritingColor_ < 5 ? skywritingColor_ + 1 : 0;
            skywritingColor_ = index;   // +0x35c
            if (host) effectDrop_ = host->CreateEffect(cRZString(kSkywritingEffectNames[index]));
            if (effectDrop_ && skywritingActive_) effectDrop_->Start(0);   // +0x360
            return true;
        }
    } else if (command == 7) {
        if (IsOccupantGroup(kAirplaneGroupFire)) {
            skywritingActive_ = !skywritingActive_;
            result = true;
        }
        if (IsOccupantGroup(kAirplaneGroupPolice)) {
            result = true;
            if (!skywritingActive_) {
                if (0 < stateCounter_) --stateCounter_;
                skywritingActive_ = true;
                skywritingTimer_ = 2.0f;   // 0x40000000
            }
        }
    }
    return result;
}

// @ 0x1006d1e62.
void cSC4Airplane::UpdateEffects() {
    IAircraftHost* host = HostFor(*this);
    if (host) {
        const int zoom = host->ViewZoom();
        const bool paused = host->SimulatorPaused() && !host->SimulatorAdvancing();
        if (effectPlanepropStill_ && effectPlaneprop_) {
            bool handled = false;
            if (std::fabs(controlWish_[3]) <= 0.0001f && std::fabs(controlWish_[2]) <= 0.0001f) {
                const float speedSquared = velocity_.x * velocity_.x + velocity_.y * velocity_.y +
                                           velocity_.z * velocity_.z;
                if (std::sqrt(speedSquared) <= 0.1f && effectPlaneprop_->IsPlaying()) {
                    effectPlaneprop_->Stop(true);
                    effectPlanepropStill_->Start(0);
                    handled = true;
                }
            }
            // Quirk: when the running prop is not playing, the fallback starts it even in the stationary case above.
            if (!handled && !effectPlaneprop_->IsPlaying()) {
                effectPlaneprop_->Start(0);
                effectPlanepropStill_->Stop(true);
            }
        }
        if (effectIndicate_) {
            bool running = false;
            if (zoom == 1 && !paused) {
                if (FindState(kAirplaneStatePlayerDrive)) {
                    cS3DTransform transform;
                    transform.translation[0] = position_.x;
                    transform.translation[1] = GetAltitude(position_);
                    transform.translation[2] = position_.z;
                    effectIndicate_->SetTransform(transform);
                    if (!effectIndicate_->IsPlaying()) effectIndicate_->Start(0);
                    running = true;
                }
            }
            if (!running && effectIndicate_->IsPlaying()) effectIndicate_->Stop(false);
        }
        if (effectDrop_) {
            if (state_ < AirplaneState::kTakingOff) skywritingActive_ = false;
            if (!paused && zoom == 1 && skywritingActive_) {
                effectDrop_->Start(0);
            } else {
                effectDrop_->Stop(false);
            }
            if (IsOccupantGroup(kAirplaneGroupFire)) effectDrop_->SetPosition(position_);
        }
    }
    cSC4Automaton::UpdateEffects();
}

// @ 0x1006d2454. Over 20 samples 16 m apart, the largest rise of the terrain/water height relative to the start.
float cSC4Airplane::EvaluateClearance(const cS3DVector3& start, const cS3DVector3& direction) const {
    IAircraftHost* host = HostFor(*this);
    if (!host) return 0.0f;
    cS3DVector3 point = start;
    float best = 0.0f;
    for (int sample = 1; sample <= 0x14; ++sample) {
        point.x += direction.x * 16.0f;
        point.y += direction.y * 16.0f;
        point.z += direction.z * 16.0f;
        const bool inBounds = host->TerrainPointInBounds(point.x, point.z);
        float height = GetAltitude(point);
        if (inBounds) height = host->EffectsWaterHeight(point.x, point.z);
        const float slope = (height - start.y) / (static_cast<float>(sample) * 16.0f);
        if (slope > best) best = slope;
    }
    return best;
}

// @ 0x1006d2db0. Searches a +/-100 m square for an active fire occupant and returns its position.
bool cSC4Airplane::FindFireNearLocation(const cS3DVector3& location, cS3DVector3& fire) const {
    IAircraftHost* host = HostFor(*this);
    if (!host) return false;
    const cS3DVector3 minCorner{location.x - 100.0f, -FLT_MAX, location.z - 100.0f};
    const cS3DVector3 maxCorner{location.x + 100.0f, FLT_MAX, location.z + 100.0f};
    return host->FindFireOccupant(minCorner, maxCorner, fireFilter_, fire);
}

// @ 0x1006d54c6.
void cSC4Airplane::SetDispatchState(std::uint32_t state) {
    if (dispatchState_ == state) return;
    dispatchState_ = state;   // +0x46c
    SendCallbackMessage(7, state);
}

// @ 0x1006d54f8.
std::uint16_t cSC4Airplane::GetCallNumber() const { return callNumber_; }

// @ 0x1006d5506.
void cSC4Airplane::SetCallNumber(std::uint16_t callNumber) { callNumber_ = callNumber; }

// @ 0x1006d5514.
std::uint32_t cSC4Airplane::GetDispatchState() const { return dispatchState_; }

}  // namespace sc4
