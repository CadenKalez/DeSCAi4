// cSC4Airplane — see Airplane.h for the recovered layout, quirks and the PARTIAL list.
#include "sc4/Airplane.h"

// The flight model is transcribed from the disassembly in its single-precision operation order; never contract to FMA.
#pragma STDC FP_CONTRACT OFF

#include <cfloat>
#include <cmath>
#include <cstring>

#include "sc4/AutomataStates.h"
#include "sc4/GeometryUtilities.h"
#include "sc4/OccupantFilters3.h"

namespace sc4 {

// __GLOBAL__sub_I_SC4Airplane.cpp @ 0x1006d5c2f..0x1006d5dd6.
const char* const kSkywritingEffectNames[6] = {
    "skywritingwhite", "skywritingyellow", "skywritinggreen",
    "skywritingblue",  "skywritingpurple", "skywritingred",
};

namespace {

// ---- flight-model math, in the operation order of the inlined S3D code (checked against 0x1006cff10) ----------------
// nS3D::k_one (0x100b8f8fc) over the length; the squared length sums z^2 + (y^2 + x^2).
cS3DVector3 Normalized(const cS3DVector3& v) {
    const float inverse = 1.0f / std::sqrt(v.z * v.z + (v.y * v.y + v.x * v.x));
    return {v.x * inverse, v.y * inverse, v.z * inverse};
}

// The rotation terms of the quaternion (axis * sin, cos), as cS3DTransform::Rotate forms them.
struct QuatTerms {
    float r[9];
    QuatTerms(const cS3DVector3& axis, float s, float w) {
        const float qx = axis.x * s, qy = axis.y * s, qz = axis.z * s;
        const float xx2 = qx * qx + qx * qx, yy2 = qy * qy + qy * qy, zz2 = qz * qz + qz * qz;
        const float xz2 = qx * qz + qx * qz, xy2 = qx * qy + qx * qy, yz2 = qy * qz + qy * qz;
        const float wx2 = w * qx + w * qx, wy2 = w * qy + w * qy, wz2 = w * qz + w * qz;
        r[0] = (1.0f - yy2) - zz2; r[1] = xy2 + wz2; r[2] = xz2 - wy2;
        r[3] = xy2 - wz2; r[4] = (1.0f - xx2) - zz2; r[5] = yz2 + wx2;
        r[6] = xz2 + wy2; r[7] = yz2 - wx2; r[8] = (1.0f - xx2) - yy2;
    }
    cS3DVector3 Apply(const cS3DVector3& v) const {
        return {(v.x * r[0] + v.y * r[1]) + v.z * r[2], (v.x * r[3] + v.y * r[4]) + v.z * r[5],
                (v.x * r[6] + v.y * r[7]) + v.z * r[8]};
    }
};

QuatTerms AxisAngle(const cS3DVector3& unitAxis, float halfAngle) {
    return QuatTerms(unitAxis, std::sin(halfAngle), std::cos(halfAngle));   // ___sincosf_stret
}

// The plane's basis for roll / pitch / yaw, inlined identically in CalculateForces and UpdateOrientation: the model
// facing (0x100b8fa10) and up (0x100b8fa00) axes rolled about the model's +X (0x100b8fa1c), yawed about world up
// (0x100b8fa28), the up axis then pitched about the new nose; columns c = |up x nose|, d = nose x c, nose.
struct AirplaneBasis {
    cS3DVector3 yawedNose;   // before normalising (UpdateOrientation hands it back)
    cS3DVector3 nose, c, d;
    AirplaneBasis(float roll, float pitch, float yaw) {
        const cS3DVector3& f = kAircraftModelFacing;
        const cS3DVector3& u = kAircraftModelSide;
        const QuatTerms rollTerms = AxisAngle(Normalized(kAircraftModelUp), roll * 0.5f);
        const QuatTerms yawTerms = AxisAngle(Normalized(kAircraftWorldUp), yaw * 0.5f);
        yawedNose = yawTerms.Apply(rollTerms.Apply(f));
        nose = Normalized(yawedNose);
        const cS3DVector3 up = AxisAngle(nose, pitch * -0.5f).Apply(yawTerms.Apply(rollTerms.Apply(u)));
        c = Normalized({nose.z * up.y - nose.y * up.z, up.z * nose.x - up.x * nose.z, up.x * nose.y - up.y * nose.x});
        d = {nose.y * c.z - nose.z * c.y, nose.z * c.x - nose.x * c.z, nose.x * c.y - nose.y * c.x};
    }
};

// (v < lo) ? lo : min(hi, v), the clamp of the compiled MINSS / CMPLTSS pairs
float ClampControl(float v, float lo, float hi) {
    const float capped = hi < v ? hi : v;
    return v < lo ? lo : capped;
}

// 1 / |(x, z)| with the y term added as 0 (x^2 + 0, then + z^2), and the vector scaled by it (y becomes 0 * scale)
cS3DVector3 GroundDirection(const cS3DVector3& v) {
    const float inverse = 1.0f / std::sqrt(v.z * v.z + (v.x * v.x + 0.0f));
    return {v.x * inverse, inverse * 0.0f, v.z * inverse};
}

// One controller step of the roll/pitch towards `target`, at most `step` either way (MINSS / CMPLTSS).
float StepTowards(float current, float target, float step) {
    const float delta = target - current;
    const float capped = step < delta ? step : delta;
    return (delta < -step ? -step : capped) + current;
}

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
    takeOffStep_ = field3e4_ = 0;                          // +0x3e0
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

// The trial flight of CalculateClimbSlope / CalculateDescentSlope / CalculateTurnRate (inlined in each): the velocity
// gains force / mass * 0.1 (capped at the high speed limit, zeroed below 1e-4), then UpdateOrientation turns the plane
// for 0.1 s from the old velocity, writing the new nose into `forward`. Returns the new velocity's y change.
bool cSC4Airplane::TrialFlightStep(cS3DVector3& velocity, cS3DVector3& forward, const AirplaneControls& controls) {
    const cS3DVector3 force = CalculateForces(velocity, forward, controls);
    const float inverseMass = 1.0f / mass_;
    cS3DVector3 next = {velocity.x + inverseMass * force.x * 0.1f, velocity.y + inverseMass * force.y * 0.1f,
                        velocity.z + inverseMass * force.z * 0.1f};
    const float lengthSquared = next.z * next.z + (next.x * next.x + next.y * next.y);
    const float length = std::sqrt(lengthSquared);
    float scale = 1.0f;
    bool rescale = false;
    if (0.0001f > length) {
        if (lengthSquared > 0.0f) { scale = 0.0f / length; rescale = true; }
    } else if (lengthSquared > 0.0f && length > maxSpeedHigh_) {
        scale = maxSpeedHigh_ / length;
        rescale = true;
    }
    if (rescale) next = {next.x * scale, next.y * scale, next.z * scale};
    const float change = std::fabs(next.y - velocity.y);
    cS3DTransform scratch;
    UpdateOrientation(velocity, forward, controls, scratch, 0.1f);
    velocity = next;
    return 0.05f > change;
}

// @ 0x1006cdbb2. Fly at the low speed limit along (0, sin, cos) of +0x3ac with full throttle and lift until the vertical
// speed settles (at most 100 steps); the slope is |vy / vz|.
float cSC4Airplane::CalculateClimbSlope() {
    const float s = std::sin(climbAngle_), c = std::cos(climbAngle_);
    cS3DVector3 forward = {0.0f, s, c};
    cS3DVector3 velocity = {maxSpeedLow_ * 0.0f, maxSpeedLow_ * s, maxSpeedLow_ * c};
    AirplaneControls controls;
    controls.steer[0] = 1.0f;
    controls.throttle = 1.0f;
    controls.lift = 1.0f;
    for (int steps = 100; steps != 0; --steps)
        if (TrialFlightStep(velocity, forward, controls)) break;
    return std::fabs(velocity.y / velocity.z);
}

// @ 0x1006cde04. The same trial flight with steer[0] = -1 and the throttle of tuning +0x454.
float cSC4Airplane::CalculateDescentSlope() {
    const float s = std::sin(climbAngle_), c = std::cos(climbAngle_);
    cS3DVector3 forward = {0.0f, s, c};
    cS3DVector3 velocity = {maxSpeedLow_ * 0.0f, maxSpeedLow_ * s, maxSpeedLow_ * c};
    AirplaneControls controls;
    controls.steer[0] = -1.0f;
    controls.throttle = tunings_[7];
    controls.lift = 1.0f;
    for (int steps = 100; steps != 0; --steps)
        if (TrialFlightStep(velocity, forward, controls)) break;
    return std::fabs(velocity.y / velocity.z);
}

// @ 0x1006ce05e. A banked turn (steer {+0x458, 1, 1}, throttle +0x454): after each step the nose's turn, acos of its
// dot product with the previous nose (clamped to [-1, 1]) over 0.1 s, until the rate changes by less than 0.001.
float cSC4Airplane::CalculateTurnRate() {
    const float s = std::sin(climbAngle_), c = std::cos(climbAngle_);
    cS3DVector3 forward = {0.0f, s, c};
    cS3DVector3 velocity = {maxSpeedLow_ * 0.0f, s * maxSpeedLow_, maxSpeedLow_ * c};
    AirplaneControls controls;
    controls.steer[0] = tunings_[8];
    controls.steer[1] = 1.0f;
    controls.steer[2] = 1.0f;
    controls.throttle = tunings_[7];
    controls.lift = 1.0f;
    cS3DVector3 previous = {0.0f, s, c};
    float rate = 0.0f;
    for (int steps = 100; steps != 0; --steps) {
        TrialFlightStep(velocity, forward, controls);   // its vertical-speed test is not used here
        const float dot = previous.z * forward.z + (previous.x * forward.x + previous.y * forward.y);
        const float capped = 1.0f < dot ? 1.0f : dot;
        const float newRate = std::acos(dot < -1.0f ? -1.0f : capped) / 0.1f;
        const bool settled = 0.001f > std::fabs(newRate - rate);
        previous = forward;
        rate = newRate;
        if (settled) break;
    }
    return rate;
}

// @ 0x1006cff10. The net force on the plane for `velocity` (the nose direction `forward` stands in while it is below
// 1e-4): the model's facing/up axes rolled (+0x378, about the model's +X), yawed (+0x380, about world up) and pitched
// (+0x37c, about the rolled nose) give the wing's lift axis; lift follows the angle between velocity and nose through the
// +0x3d0..+0x3dc curve, thrust is +0x3b8 * throttle along the nose (cut when reversing against the motion in states 0/1),
// drag opposes the velocity and, below take-off speed, a ground term holds the plane up against gravity.
cS3DVector3 cSC4Airplane::CalculateForces(const cS3DVector3& velocity, const cS3DVector3& forward,
                                          const AirplaneControls& controls) const {
    const float thrustScale = field3b8_;
    const float dragReference = 0.5f * maxSpeedHigh_ * maxSpeedHigh_ * dragArea_ * 0.1f;
    const float speed = std::sqrt(velocity.z * velocity.z + (velocity.x * velocity.x + velocity.y * velocity.y));
    const cS3DVector3& heading = speed < 0.0001f ? forward : velocity;
    const cS3DVector3 unitHeading = Normalized(heading);

    // the model's side axis (kModelFacing x the 0x100b8fa00 axis) carried into the plane's basis is the wing axis
    const cS3DVector3& f = kAircraftModelFacing;
    const cS3DVector3& u = kAircraftModelSide;
    const cS3DVector3 side = {f.y * u.z - f.z * u.y, f.z * u.x - f.x * u.z, f.x * u.y - f.y * u.x};
    const AirplaneBasis basis(roll_, pitch_, yaw_);
    const cS3DVector3& c = basis.c;
    const cS3DVector3& d = basis.d;
    const cS3DVector3& nose = basis.nose;
    const cS3DVector3 wing = Normalized({(c.x * side.x + d.x * side.y) + nose.x * side.z,
                                         (c.y * side.x + d.y * side.y) + nose.y * side.z,
                                         (c.z * side.x + d.z * side.y) + nose.z * side.z});

    // angle of attack between the heading and the nose, from their elevations
    const float cosine = unitHeading.y * forward.y +
                         std::sqrt(1.0f - unitHeading.y * unitHeading.y) * std::sqrt(1.0f - forward.y * forward.y);
    const float capped = 1.0f < cosine ? 1.0f : cosine;
    float angle = std::acos(cosine < -1.0f ? -1.0f : capped);
    const float halfPi = 1.5707964f;
    const float cappedAngle = halfPi < angle ? halfPi : angle;
    angle = angle < 0.0f ? 0.0f : cappedAngle;

    const float pressure = 0.5f * speed * speed * dragArea_ * (controls.lift * field3bc_ + 1.0f);
    float base, slope;
    if (approachAngle_ >= angle) {
        base = approachScale_;
        slope = ((approachFactor_ - approachScale_) * angle) / approachAngle_;
    } else {
        base = approachFactor_;
        slope = ((field3dc_ - approachFactor_) * (angle - approachAngle_)) / (halfPi - approachAngle_);
    }
    const float lift = (slope + base) * pressure;
    const float liftX = (unitHeading.z * wing.y - unitHeading.y * wing.z) * lift;
    const float liftY = (wing.z * unitHeading.x - unitHeading.z * wing.x) * lift;
    const float liftZ = (wing.x * unitHeading.y - wing.y * unitHeading.x) * lift;

    float thrust = thrustScale * controls.throttle;
    const int state = static_cast<int>(state_);
    if (controls.throttle < 0.0f && state < 2 &&
        (velocity.x * forward.x + velocity.y * forward.y) + velocity.z * forward.z < 0.0f)
        thrust = 0.0f;   // no reverse thrust against the motion while flying or landing
    const float gravity = -9.8f * mass_;
    const float drag = (angle * angle + 0.1f) * ((thrustScale / dragReference) * pressure);
    float ground = 0.0f;
    if (state <= 1 && -gravity > liftY) ground = (1.0f - liftY / -gravity) * (speed * 0.1f * mass_);
    const float resist = drag + ground;
    return {((liftX + 0.0f) + forward.x * thrust) - unitHeading.x * resist,
            ((gravity + liftY) + forward.y * thrust) - unitHeading.y * resist,
            ((liftZ + 0.0f) + forward.z * thrust) - unitHeading.z * resist};
}

// @ 0x1006d14e8. Steps roll (+0x378) towards (steer[0] + value14) * +0x3ac and, on the ground (states 2/3), pitch
// (+0x37c) towards steer[1] * +0x3b0, each by at most rate * delta (+0x394 / +0x398); while flying or landing the pitch
// is reset to 0 and the roll only moves at or above the low speed limit (+0x38c). Yaw (+0x380) advances by
// delta * steer[2] * +0x3b4, twice as fast in the air. `transform` receives the plane's basis (four rotation updates in
// the original: change counter + 4, rotation flag) and `heading` the yawed, unnormalised nose.
void cSC4Airplane::UpdateOrientation(const cS3DVector3& direction, cS3DVector3& heading,
                                     const AirplaneControls& controls, cS3DTransform& transform, float delta) {
    const int state = static_cast<int>(state_);
    const bool slow = state <= 1 &&
        maxSpeedLow_ > std::sqrt(direction.z * direction.z + (direction.x * direction.x + direction.y * direction.y));
    if (!slow) {
        roll_ = StepTowards(roll_, (controls.steer[0] + controls.value14) * climbAngle_, climbScale_ * delta);
        if (state > 1) pitch_ = StepTowards(pitch_, controls.steer[1] * descentAngle_, descentScale_ * delta);
    }
    if (slow || state <= 1) pitch_ = 0.0f;
    float turn = delta * controls.steer[2] * takeoffAngle_;
    if (state < 2) turn = turn + turn;
    yaw_ = turn + yaw_;

    const AirplaneBasis basis(roll_, pitch_, yaw_);
    for (int i = 0; i < 3; ++i) {
        transform.rotation[i][0] = (&basis.c.x)[i];
        transform.rotation[i][1] = (&basis.d.x)[i];
        transform.rotation[i][2] = (&basis.nose.x)[i];
    }
    transform.flags = std::uint8_t(transform.flags | 4);
    transform.changeCounter = std::uint8_t(transform.changeCounter + 4);
    heading = basis.yawedNose;
}

// @ 0x1006cee14 PARTIAL: the airport/runway-aware flight step is not ported.
void cSC4Airplane::UpdatePhysics(float delta) { (void)delta; }

// @ 0x1006d0cd0 PARTIAL: the collision sweep is not ported.
void cSC4Airplane::DetectCollisions() {}

// @ 0x1006ce990 PARTIAL: the goal selection (fire search / landing / take-off) is not ported.
void cSC4Airplane::AI_PursueGoal(float delta) { (void)delta; }

// @ 0x1006d2f12. Fly to `destination`; on the ground (states 0..2) this only runs the take-off and answers false. In
// the air the leg starts at +0x3e8 (reset to the plane when it is within 640 of the destination but already past it).
// Beyond 640 from the leg start the plane turns straight at the destination. Within it, a waypoint 640 to the side of
// the leg start sets up a turn: first a turn towards it (+0x424 counts it), then a procedure turn through it onto the
// destination; heading away from the destination after the turn restarts the leg. The altitude follows the
// destination's height, held above the terrain clamp and the tallest building except within the descent slope's reach.
// True when on the final leg and close enough to descend: sqrt(height above the ground there) * 0.15 * speed > distance.
bool cSC4Airplane::AI_GotoDestination(const cS3DVector3& destination, float delta) {
    if (static_cast<int>(state_) <= 2) {
        AI_TakeOff(delta);
        return false;
    }
    cS3DVector3 origin = {field3e8_, field3ec_, field3f0_};
    const float ux = destination.x - origin.x, uy = destination.y - origin.y, uz = destination.z - origin.z;
    const float legLength = std::sqrt(uz * uz + (uy * uy + ux * ux));
    const float inverseLeg = 1.0f / legLength;
    const cS3DVector3 leg = {ux * inverseLeg, inverseLeg * uy, inverseLeg * uz};
    const float wx = destination.x - position_.x, wy = destination.y - position_.y, wz = destination.z - position_.z;
    const float distance = std::sqrt(wz * wz + (wy * wy + wx * wx));
    const float inverseTo = 1.0f / distance;
    const cS3DVector3 to = {wx * inverseTo, inverseTo * wy, inverseTo * wz};
    if (640.0f > distance && -0.5f > leg.z * to.z + (leg.x * to.x + leg.y * to.y)) {
        field3e8_ = position_.x;
        field3ec_ = position_.y;
        field3f0_ = position_.z;
        field434_ = 0;
        origin = position_;
    }
    bool arrived;
    if (640.0f > legLength) {
        const cS3DVector3& w = kAircraftWorldUp;
        const cS3DVector3 side = Normalized({leg.y * w.z - leg.z * w.y, leg.z * w.x - w.z * leg.x, leg.x * w.y - leg.y * w.x});
        cS3DVector3 waypoint = {origin.x + side.x * 640.0f, origin.y + side.y * 640.0f, side.z * 640.0f + origin.z};
        ClipToWorld(waypoint);
        if (field424_ == 0) {
            if (AI_TurnToVector({waypoint.x - field3e8_, waypoint.y - field3ec_, waypoint.z - field3f0_}, delta)) {
                ++field424_;
                field434_ = 0;
            }
            arrived = false;
        } else {
            arrived = AI_FollowVectorAndTurn(
                {waypoint.x - position_.x, waypoint.y - position_.y, waypoint.z - position_.z}, waypoint,
                {destination.x - waypoint.x, destination.y - waypoint.y, destination.z - waypoint.z}, delta);
            if (field434_ == 2 &&
                -0.5f > to.z * orientation_.z + (to.x * orientation_.x + to.y * orientation_.y)) {
                field3e8_ = position_.x;
                field3ec_ = position_.y;
                field3f0_ = position_.z;
                field424_ = 0;
                field434_ = 0;
            }
        }
    } else {
        arrived = AI_TurnToVector({destination.x - position_.x, destination.y - position_.y, destination.z - position_.z},
                                  delta);
    }

    IAircraftHost* host = HostFor(*this);
    float target = destination.y;
    if (host) {
        const float terrain = host->AltitudeClamp();
        const float buildings = host->TallestBuildingAltitude();
        const float ceiling = buildings > terrain ? buildings : terrain;   // MAXSS
        if (ceiling > target) {
            const float reach = target + descentSlope_ * distance;
            target = reach > ceiling ? ceiling : reach;
        }
    }
    AI_ApproachAltitude(target, false, delta);
    const float speed = std::sqrt(velocity_.z * velocity_.z + (velocity_.x * velocity_.x + velocity_.y * velocity_.y));
    const float height = position_.y;
    const float ground = GetAltitude(destination);
    if (!arrived) return false;
    return std::sqrt(height - ground) * (speed * 0.15f) > distance;
}

// @ 0x1006d33f8 PARTIAL: the runway/approach landing sequence is not ported.
bool cSC4Airplane::AI_Land(float delta) {
    (void)delta;
    return false;
}

// @ 0x1006d3f70. Below a forward speed of 0.1 (velocity . orientation) every control is zeroed and the plane counts as
// stopped. Otherwise the controls ease (at tuning * delta per step) towards full roll input, no pitch or yaw input, a
// reverse throttle of -0.03 * speed clamped to [-1, 0], full lift and nothing at +0x328.
bool cSC4Airplane::AI_Stop(float delta) {
    const float speed = velocity_.z * orientation_.z + (velocity_.x * orientation_.x + velocity_.y * orientation_.y);
    if (0.1f > speed) {
        for (float& c : controlWish_) c = 0.0f;
        controlAim_ = 0.0f;
        controlAim2_ = 0.0f;
        return true;
    }
    const float brake = speed * -0.03f;
    const float capped = 0.0f < brake ? 0.0f : brake;
    const float targets[4] = {1.0f, 0.0f, 0.0f, brake < -1.0f ? -1.0f : capped};   // INSERTPS 0x34: lane 3
    const float rates[4] = {tunings_[3], tunings_[2], tunings_[10], tunings_[0]};   // +0x444 +0x440 +0x460 +0x438
    for (int i = 0; i < 4; ++i) controlWish_[i] = StepTowards(controlWish_[i], targets[i], rates[i] * delta);
    controlAim_ = StepTowards(controlAim_, 1.0f, tunings_[5] * delta);     // +0x44c
    controlAim2_ = StepTowards(controlAim2_, 0.0f, delta * tunings_[11]);  // +0x464
    return false;
}

// @ 0x1006d40e2. The take-off sequence, step in +0x3e0: (0) pick a runway (PickRunwayDirection, taking off), keep the
// position at +0x3e8, the runway start in points_[0] and a point 200 down the runway in points_[1]; (1) taxi to the
// start, towards the foot of the perpendicular on the runway line while more than 10 off it, and stop within 3; (2)
// turn onto the runway; (3) keep the heading and climb to 100 above the current height. True once in state 3.
bool cSC4Airplane::AI_TakeOff(float delta) {
    switch (takeOffStep_) {
    case 0: {
        field3e4_ = 0;
        field434_ = 0;
        cS3DVector3 start = {0.0f, 0.0f, 0.0f}, direction = {0.0f, 0.0f, 0.0f};   // nS3D::k_zero
        PickRunwayDirection(position_, start, direction, false);
        field3e8_ = position_.x;
        field3ec_ = position_.y;
        field3f0_ = position_.z;
        points_[0] = start;
        points_[1] = {start.x + direction.x * 200.0f, start.y + direction.y * 200.0f, start.z + direction.z * 200.0f};
        takeOffStep_ = 1;
        break;
    }
    case 1: {
        const cS3DVector3& start = points_[0];
        const float dx = start.x - position_.x, dy = start.y - position_.y, dz = start.z - position_.z;
        if (3.0f > std::sqrt(dz * dz + (dy * dy + dx * dx))) {
            if (AI_Stop(delta)) takeOffStep_ = 2;
            break;
        }
        const float rx = points_[1].x - start.x, ry = points_[1].y - start.y, rz = points_[1].z - start.z;
        const float inverse = 1.0f / std::sqrt(rz * rz + (ry * ry + rx * rx));
        const float runway[3] = {rx * inverse, ry * inverse, inverse * rz};
        if (SC4GeometryUtilities::PointDistanceToRay(&position_.x, &start.x, runway) > 10.0f) {
            // the foot of the perpendicular from the plane on the runway line: runway x ((start - position) x runway)
            const cS3DVector3 c = {dy * runway[2] - dz * runway[1], dz * runway[0] - dx * runway[2],
                                   dx * runway[1] - dy * runway[0]};
            AI_Taxi({(runway[1] * c.z - runway[2] * c.y) + position_.x, (runway[2] * c.x - c.z * runway[0]) + position_.y,
                     (c.y * runway[0] - c.x * runway[1]) + position_.z},
                    delta);
        } else {
            AI_Taxi(start, delta);
        }
        break;
    }
    case 2:
        if (AI_TurnToVector({points_[1].x - points_[0].x, points_[1].y - points_[0].y, points_[1].z - points_[0].z},
                            delta))
            takeOffStep_ = 3;
        break;
    case 3:
        AI_TurnToVector({points_[1].x - points_[0].x, points_[1].y - points_[0].y, points_[1].z - points_[0].z}, delta);
        AI_ApproachAltitude(position_.y + 100.0f, false, delta);
        break;
    default:
        break;
    }
    return static_cast<int>(state_) == 3;
}

// @ 0x1006d44cc. Taxi towards `target` on the ground: steer[2] eases towards the turn side (clamped to +/-0.2); the
// throttle aims at a slow crawl while the heading is more than 12 degrees off (-0.002 * speed in [-0.1, 0]), otherwise
// at 0.02 * distance - 0.1 * speed in [-0.2, 0.03]; every other control eases to 0.
void cSC4Airplane::AI_Taxi(const cS3DVector3& target, float delta) {
    const float dx = target.x - position_.x, dy = target.y - position_.y, dz = target.z - position_.z;
    const cS3DVector3 toTarget = GroundDirection({dx, dy, dz});
    const cS3DVector3 nose = GroundDirection(orientation_);
    const float speed = std::sqrt(velocity_.z * velocity_.z + (velocity_.y * velocity_.y + velocity_.x * velocity_.x));
    const float turnSide = toTarget.z * nose.x - toTarget.x * nose.z;
    const float cosine = (nose.y * toTarget.y + toTarget.x * nose.x) + nose.z * toTarget.z;
    const float angle = std::acos(ClampControl(cosine, -1.0f, 1.0f));
    controlWish_[2] = StepTowards(controlWish_[2], ClampControl(turnSide, -0.2f, 0.2f), tunings_[10] * delta);
    float throttle;
    if (angle > 0.20943952f) {
        throttle = ClampControl(speed * -0.002f, -0.1f, 0.0f);
    } else {
        const float distance = std::sqrt((dx * dx + dy * dy) + dz * dz);
        throttle = ClampControl(speed * -0.1f + distance * 0.02f, -0.2f, 0.03f);
    }
    controlWish_[3] = StepTowards(controlWish_[3], throttle, tunings_[0] * delta);
    controlWish_[1] = StepTowards(controlWish_[1], 0.0f, tunings_[2] * delta);
    controlWish_[0] = StepTowards(controlWish_[0], 0.0f, tunings_[3] * delta);
    controlAim_ = StepTowards(controlAim_, 0.0f, tunings_[5] * delta);
    controlAim2_ = StepTowards(controlAim2_, 0.0f, delta * tunings_[11]);
}

// @ 0x1006d4a0c. Altitude hold: lift (+0x324) eases towards `useLift`, +0x328 towards 0. Within 5 of `altitude` the
// roll input only fights a vertical speed beyond 0.5 (towards -/+ tuning +0x45c) and the throttle aims at +0x454 (with
// lift) or full; above it (error > 0) the input climbs and the throttle doubles +0x454; below it the input aims at
// +0x458 down to an error of -5, then full down, with throttle +0x454. Returns 0 holding, 1 climbing, -1 descending.
// The controls are clamped afterwards to [-1, 1] (the throttle's floor is +0x43c on the ground).
int cSC4Airplane::AI_ApproachAltitude(float altitude, bool useLift, float delta) {
    const float throttleStep = tunings_[0] * delta;   // +0x438
    controlAim_ = StepTowards(controlAim_, static_cast<float>(useLift), throttleStep);
    controlAim2_ = StepTowards(controlAim2_, 0.0f, tunings_[11] * delta);   // +0x464
    const float error = altitude - position_.y;
    float& input = controlWish_[0];
    float throttleTarget;
    int result;
    if (5.0f > std::fabs(error)) {
        const float vertical = velocity_.y;
        const float limit = tunings_[9];   // +0x45c
        if (-0.5f > vertical) {
            if (limit > input) input = StepTowards(input, limit, tunings_[3] * 0.1f * delta);
        } else if (vertical > 0.5f && input > -limit) {
            input = StepTowards(input, -limit, tunings_[3] * 0.1f * delta);
        }
        throttleTarget = useLift ? tunings_[7] : 1.0f;
        result = 0;
    } else if (error > 0.0f) {
        input = StepTowards(input, 1.0f, tunings_[3] * 0.1f * delta);
        throttleTarget = useLift ? tunings_[7] + tunings_[7] : 1.0f;
        result = 1;
    } else {
        if (error > -5.0f) input = StepTowards(input, tunings_[8], delta * tunings_[3]);   // no 0.1 here
        else input = StepTowards(input, -1.0f, tunings_[3] * 0.1f * delta);
        throttleTarget = tunings_[7];
        result = -1;
    }
    controlWish_[3] = StepTowards(controlWish_[3], throttleTarget, throttleStep);
    for (int i = 0; i < 3; ++i) controlWish_[i] = ClampControl(controlWish_[i], -1.0f, 1.0f);
    controlWish_[3] = ClampControl(controlWish_[3], static_cast<int>(state_) >= 2 ? tunings_[1] : -1.0f, 1.0f);
    controlAim_ = ClampControl(controlAim_, 0.0f, 1.0f);
    controlAim2_ = ClampControl(controlAim2_, -tunings_[9], tunings_[9]);
    return result;
}

// @ 0x1006d4df8. A procedure turn through `turnPoint`, phase in +0x434: (0) follow the inbound ray until the distance
// left along the nose, at the current ground speed, is under twice the time to turn the angle between the legs at the
// turn rate (+0x3cc), capped at 5; (1) bank towards the outbound heading (steer[1] only beyond 2 degrees off) until
// within 6 degrees; (2) follow the outbound ray. True while on the outbound leg.
bool cSC4Airplane::AI_FollowVectorAndTurn(const cS3DVector3& inbound, const cS3DVector3& turnPoint,
                                          const cS3DVector3& outbound, float delta) {
    const cS3DVector3 out = GroundDirection(outbound);
    const cS3DVector3 nose = GroundDirection(orientation_);
    if (field434_ == 0) {
        const cS3DVector3 in = GroundDirection(inbound);
        const float legCosine = ClampControl(in.z * out.z + (in.y * out.y + in.x * out.x), -1.0f, 1.0f);
        const float dx = turnPoint.x - position_.x, dz = turnPoint.z - position_.z;
        const float turnTime = std::acos(legCosine) / turnRate_;
        const float along = dz * nose.z + (nose.y * 0.0f + dx * nose.x);
        const float eta = along / std::sqrt(velocity_.z * velocity_.z + (velocity_.x * velocity_.x + 0.0f));
        const float lead = turnTime + turnTime;
        if ((5.0f < lead ? 5.0f : lead) > eta) {
            field434_ = 1;
        } else {
            AI_FollowRay(turnPoint, inbound, delta);
            return false;
        }
    }
    if (field434_ == 1) {
        const float turnSide = out.z * nose.x - out.x * nose.z;
        const float angle = std::acos(ClampControl(out.z * nose.z + (out.y * nose.y + out.x * nose.x), -1.0f, 1.0f));
        if (angle > 0.034906585f) controlWish_[1] = StepTowards(controlWish_[1], turnSide, tunings_[2] * delta);
        controlWish_[2] = StepTowards(controlWish_[2], turnSide, tunings_[10] * delta);
        if (!(0.10471976f > angle)) return false;
        field434_ = 2;
    } else if (field434_ != 2) {
        return false;
    }
    AI_FollowRay(turnPoint, outbound, delta);
    return true;
}

// @ 0x1006d51a2. Follow the ground projection of the ray (origin, direction): steer[1] eases towards the side of the
// ray scaled by the distance (1 at 100 and beyond), steer[2] towards the heading error ((1 - cos) or (cos - 1) by the
// turn side, * 1.5) plus 0.001 * distance beyond 5. True once within 5 of the ray and within 6 degrees of its heading.
bool cSC4Airplane::AI_FollowRay(const cS3DVector3& origin, const cS3DVector3& direction, float delta) {
    const float dx = direction.x, dz = direction.z;
    const float inverse3 = 1.0f / std::sqrt((dx * dx + 0.0f) + dz * dz);
    const float ray3[3] = {dx * inverse3, inverse3 * 0.0f, dz * inverse3};
    const float inverse2 = 1.0f / std::sqrt(dz * dz + dx * dx);
    const float ray2[2] = {dx * inverse2, inverse2 * dz};
    const float origin3[3] = {origin.x, 0.0f, origin.z};
    const float origin2[2] = {origin.x, origin.z};
    const float inverseNose = 1.0f / std::sqrt(orientation_.z * orientation_.z + (orientation_.x * orientation_.x + 0.0f));
    const float nose[3] = {inverseNose * orientation_.x, inverseNose * 0.0f, inverseNose * orientation_.z};
    const float point3[3] = {position_.x, 0.0f, position_.z};
    const float point2[2] = {position_.x, position_.z};

    const float distance = SC4GeometryUtilities::PointDistanceToRay(point3, origin3, ray3);
    const float side = 0.0f < SC4GeometryUtilities::PointRelativeToRay2D(point2, origin2, ray2) ? 1.0f : -1.0f;
    const float offset = ClampControl(0.01f * distance, 0.0f, 1.0f) * side;
    controlWish_[1] = StepTowards(controlWish_[1], offset, tunings_[2] * delta);   // +0x440

    const float turnSide = nose[0] * ray3[2] - nose[2] * ray3[0];
    const float cosine = ClampControl(nose[2] * ray3[2] + (nose[1] * ray3[1] + nose[0] * ray3[0]), -1.0f, 1.0f);
    const float angle = std::acos(cosine);
    float turn = (0.0f < turnSide ? 1.0f - cosine : -1.0f + cosine) * 0.5f * 3.0f;
    if (distance > 5.0f) turn = turn + side * 0.001f * distance;
    controlWish_[2] = StepTowards(controlWish_[2], turn, delta * tunings_[10]);   // +0x460
    return 5.0f > distance && 0.10471976f > angle;
}

// @ 0x1006d258c. Picks a runway end near `position`: each end of each runway (pairs in the manager's list) is weighted
// by its clearance looking down the runway, less the climb slope (the descent slope when `landing`), floored at 0; the
// manager's random source picks one weighted end. Taking off starts at the other end and heads towards the pick;
// landing starts at the pick. +0x428 keeps the end headed for. No runway pair: the plane removes itself (vslot 0x68).
void cSC4Airplane::PickRunwayDirection(const cS3DVector3& position, cS3DVector3& start, cS3DVector3& direction,
                                       bool landing) {
    IAircraftHost* host = HostFor(*this);
    if (!host) return;
    std::vector<cS3DVector3> ends;
    host->GetRunwayList(position, ends);
    const int count = static_cast<int>(ends.size());
    if (count < 2 || (count & 1) != 0) {
        Remove(false);
        return;
    }
    std::vector<double> weights(static_cast<std::size_t>(count), 0.0);
    const float margin = landing ? descentSlope_ : climbSlope_;
    for (std::size_t i = 0; i < ends.size(); i += 2) {
        const cS3DVector3& a = ends[i];
        const cS3DVector3& b = ends[i + 1];
        const float dx = b.x - a.x, dz = b.z - a.z;
        const float inverse = 1.0f / std::sqrt((dx * dx + 0.0f) + dz * dz);
        const cS3DVector3 along = {dx * inverse, inverse * 0.0f, dz * inverse};
        const float fromA = EvaluateClearance(a, along) - margin;
        weights[i] = fromA > 0.0f ? fromA : 0.0f;   // MAXSS with 0
        const float fromB = EvaluateClearance(b, {-along.x, -along.y, -along.z}) - margin;
        weights[i + 1] = fromB > 0.0f ? fromB : 0.0f;
    }
    const std::uint32_t pick = host->RandomWeightedChoice(static_cast<std::uint32_t>(count), weights.data());
    const std::uint32_t other = pick - 1 + (~(pick * 2) & 2);   // the runway's other end
    const cS3DVector3 from = ends[landing ? pick : other];
    const cS3DVector3 to = ends[landing ? other : pick];
    start = from;
    const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    const float inverse = 1.0f / std::sqrt(dz * dz + (dy * dy + dx * dx));
    direction = {dx * inverse, dy * inverse, inverse * dz};
    field428_ = to.x;
    field42c_ = to.y;
    field430_ = to.z;
}

// @ 0x1006d2932. The approach box behind the runway end `runwayEnd` along `direction`: points_[3] is the runway end,
// [2] a leg back along the runway, [1] a leg across it (towards the city centre), [0] a leg forward again, each clipped
// to the world; the heights descend at +0x3c8 along the box. The leg is the climb from the runway end to the higher of
// the terrain clamp and the tallest building at that slope, capped by the larger city side, at least 500.
void cSC4Airplane::PickApproachBox(const cS3DVector3& runwayEnd, const cS3DVector3& direction) {
    IAircraftHost* host = HostFor(*this);
    if (!host) return;
    const float sizeX = host->CitySizeX(), sizeZ = host->CitySizeZ();
    const float largerSide = sizeZ > sizeX ? sizeZ : sizeX;   // MAXSS
    const float centreX = sizeX * 0.5f, centreZ = sizeZ * 0.5f;
    const cS3DVector3& w = kAircraftWorldUp;
    const cS3DVector3& d = direction;
    cS3DVector3 across = Normalized({w.z * d.y - w.y * d.z, d.z * w.x - w.z * d.x, d.x * w.y - w.x * d.y});
    const float facing = (centreZ - runwayEnd.z) * across.z +
                         ((0.0f - runwayEnd.y) * across.y + (centreX - runwayEnd.x) * across.x);
    if (!(facing > 0.0f)) across = {-across.x, -across.y, -across.z};
    const float terrain = host->AltitudeClamp();
    const float buildings = host->TallestBuildingAltitude();
    const float ceiling = buildings > terrain ? buildings : terrain;   // MAXSS
    const float climb = (ceiling - runwayEnd.y) / descentSlope_;
    const float leg = climb < 500.0f ? 500.0f : (largerSide < climb ? largerSide : climb);
    auto distance = [](const cS3DVector3& a, const cS3DVector3& b) {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dz * dz + (dy * dy + dx * dx));
    };
    points_[3] = runwayEnd;
    cS3DVector3 p = {runwayEnd.x - d.x * leg, runwayEnd.y - d.y * leg, runwayEnd.z - d.z * leg};
    ClipToWorld(p);
    float run = distance(p, points_[3]) + 0.0f;
    points_[2] = {p.x, descentSlope_ * run + runwayEnd.y, p.z};
    p = {across.x * leg + p.x, across.y * leg + points_[2].y, leg * across.z + p.z};
    ClipToWorld(p);
    run = distance(p, points_[2]) + run;
    points_[1] = {p.x, descentSlope_ * run + runwayEnd.y, p.z};
    p = {d.x * leg + p.x, d.y * leg + points_[1].y, d.z * leg + p.z};
    ClipToWorld(p);
    points_[0] = {p.x, (distance(p, points_[1]) + run) * descentSlope_ + runwayEnd.y, p.z};
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
    // the ground directions and their products in the machine code's grouping (oracle-checked, like AI_Taxi)
    const cS3DVector3 toTarget = GroundDirection(vector);
    const cS3DVector3 nose = GroundDirection(orientation_);
    const float turnSide = toTarget.z * nose.x - toTarget.x * nose.z;
    const float cosine = (nose.y * toTarget.y + toTarget.x * nose.x) + nose.z * toTarget.z;
    const float angle = std::acos(ClampControl(cosine, -1.0f, 1.0f));
    if (angle > 0.034906585f) controlWish_[1] = StepTowards(controlWish_[1], turnSide, tunings_[2] * delta);   // +0x440
    const float target = static_cast<int>(state_) <= 1 ? ClampControl(turnSide, -0.2f, 0.2f) : turnSide;
    controlWish_[2] = StepTowards(controlWish_[2], target, delta * tunings_[10]);   // +0x460
    return 0.10471976f > angle;
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
        takeOffStep_ = field3e4_ = 0;
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
            takeOffStep_ = field3e4_ = 0;
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
        takeOffStep_ = field3e4_ = 0;
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
