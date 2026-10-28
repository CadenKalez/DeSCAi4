// airplane_oracle - test tooling: the cSC4Airplane flight model against the retail machine code (see GameImage.h).
// Random flight states go into a fake original object (fields at their original offsets) and into the port; the
// results must agree bit for bit. Build and run with scripts/run_machine_oracle.sh airplane.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <random>

#include "GameImage.h"
#include "sc4/Airplane.h"

namespace sc4 {
struct AirplaneOracleAccess {
    static void Set(cSC4Airplane& p, const float* f, int state) {
        p.state_ = static_cast<AirplaneState>(state);
        p.roll_ = f[0]; p.pitch_ = f[1]; p.yaw_ = f[2]; p.mass_ = f[3]; p.dragArea_ = f[4];
        p.maxSpeedLow_ = f[5]; p.maxSpeedHigh_ = f[6]; p.field3b8_ = f[7]; p.field3bc_ = f[8];
        p.approachAngle_ = f[9]; p.approachScale_ = f[10]; p.approachFactor_ = f[11]; p.field3dc_ = f[12];
        p.climbScale_ = f[13]; p.descentScale_ = f[14]; p.climbAngle_ = f[15]; p.descentAngle_ = f[16];
        p.takeoffAngle_ = f[17]; p.tunings_[7] = f[18]; p.tunings_[8] = f[19];
    }
    // controls (+0x314..+0x328), the twelve tunings (+0x438), orientation (+0x8c) and velocity (+0x36c)
    static void SetMotion(cSC4Airplane& p, const float* controls, const float* tunings, const float* orientation,
                          const float* velocity) {
        for (int i = 0; i < 4; ++i) p.controlWish_[i] = controls[i];
        p.controlAim_ = controls[4]; p.controlAim2_ = controls[5];
        for (int i = 0; i < 12; ++i) p.tunings_[i] = tunings[i];
        p.orientation_ = {orientation[0], orientation[1], orientation[2]};
        p.velocity_ = {velocity[0], velocity[1], velocity[2]};
    }
    static void SetPositionState(cSC4Airplane& p, const float* position, int state) {
        p.position_ = {position[0], position[1], position[2]};
        p.state_ = static_cast<AirplaneState>(state);
    }
    static int& Phase(cSC4Airplane& p) { return p.field434_; }
    static void SetTurnRate(cSC4Airplane& p, float rate) { p.turnRate_ = rate; }
    static void Controls(const cSC4Airplane& p, float* out) {
        for (int i = 0; i < 4; ++i) out[i] = p.controlWish_[i];
        out[4] = p.controlAim_; out[5] = p.controlAim2_;
    }
};
}  // namespace sc4

using namespace sc4;
using namespace machine_oracle;

namespace {
// original offsets of the fields AirplaneOracleAccess::Set fills, in the same order
constexpr int kOffsets[20] = {0x378, 0x37c, 0x380, 0x384, 0x388, 0x38c, 0x390, 0x3b8, 0x3bc, 0x3d0,
                              0x3d4, 0x3d8, 0x3dc, 0x394, 0x398, 0x3ac, 0x3b0, 0x3b4, 0x454, 0x458};
alignas(16) unsigned char original[0x4a0];

void SetOriginal(const float* f, int state) {
    std::memset(original, 0, sizeof original);
    std::memcpy(original + 0x310, &state, 4);
    for (int i = 0; i < 20; ++i) std::memcpy(original + kOffsets[i], &f[i], 4);
}
float Field(int offset) { float v; std::memcpy(&v, original + offset, 4); return v; }

bool Same(float a, float b) { return std::memcmp(&a, &b, 4) == 0 || (std::isnan(a) && std::isnan(b)); }

struct Transform { std::uint8_t counter, flags; float rotation[9]; float translation[3]; float scale; };

int failures = 0;
void Check(bool ok, const char* what, int trial) {
    if (!ok && ++failures <= 20) std::printf("MISMATCH %s (trial %d)\n", what, trial);
}
}  // namespace

int main() {
    if (!MapGame("game-binaries/MacOS/Sim City 4 Deluxe Editionsub")) return 2;
    BindStub(0x10092af3a, dlsym(RTLD_DEFAULT, "__sincosf_stret"));
    BindStub(0x10092af8e, reinterpret_cast<const void*>(static_cast<float (*)(float)>(&acosf)));
    At<float>(0x100b8f8f8) = 0.0f;   // nS3D::k_zero
    At<float>(0x100b8f8fc) = 1.0f;   // nS3D::k_one
    // the model axes __GLOBAL__sub_I_SC4Airplane.cpp writes (Aircraft.h kAircraftModel*)
    At<float>(0x100b8fa00) = 0.0f; At<float>(0x100b8fa04) = 1.0f; At<float>(0x100b8fa08) = 0.0f;
    At<float>(0x100b8fa10) = 0.0f; At<float>(0x100b8fa14) = 0.0f; At<float>(0x100b8fa18) = 1.0f;   // kModelFacing
    At<float>(0x100b8fa1c) = 1.0f; At<float>(0x100b8fa20) = 0.0f; At<float>(0x100b8fa24) = 0.0f;
    At<float>(0x100b8fa28) = 0.0f; At<float>(0x100b8fa2c) = 1.0f; At<float>(0x100b8fa30) = 0.0f;   // kWorldUp

    using Forces = void* (*)(float*, void*, const float*, const float*, const float*);
    using Orientation = void (*)(void*, const float*, float*, const float*, void*, float);
    using Scalar = float (*)(void*);
    const auto forces = Function<Forces>(0x1006cff10);
    const auto orient = Function<Orientation>(0x1006d14e8);
    const auto climb = Function<Scalar>(0x1006cdbb2), descent = Function<Scalar>(0x1006cde04),
               turn = Function<Scalar>(0x1006ce05e);

    std::mt19937 rng(20261028);
    std::uniform_real_distribution<float> angle(-1.6f, 1.6f), unit(-1.0f, 1.0f), speed(-80.0f, 80.0f);
    cSC4Airplane port;
    const int trials = 4000;
    for (int t = 0; t < trials; ++t) {
        // realistic parameters around the constructor defaults, plus random flight attitude
        float f[20] = {angle(rng), angle(rng), angle(rng) * 2, 50.0f + 100.0f * (unit(rng) + 1), 0.5f + unit(rng) * 0.4f,
                       20.0f + 10.0f * unit(rng), 120.0f + 40.0f * unit(rng), 8000.0f + 4000.0f * unit(rng),
                       0.4f + 0.2f * unit(rng), 0.5235988f + 0.2f * unit(rng), 0.08f + 0.05f * unit(rng),
                       0.68f + 0.2f * unit(rng), 0.1f * unit(rng), 0.0654f, 0.3927f, 0.2618f + 0.1f * unit(rng),
                       0.7854f, 0.5236f, 0.2f + 0.1f * unit(rng), 0.4f + 0.2f * unit(rng)};
        if (t % 7 == 0) f[1] = 0.0f;
        const int state = static_cast<int>(rng() % 4);
        SetOriginal(f, state);
        AirplaneOracleAccess::Set(port, f, state);

        float velocity[3] = {speed(rng), speed(rng) * 0.3f, speed(rng)};
        if (t % 11 == 0) velocity[0] = velocity[1] = velocity[2] = 0.0f;
        float forward[3] = {unit(rng), unit(rng) * 0.5f, unit(rng)};
        const float len = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
        for (float& v : forward) v /= len;
        AirplaneControls controls;
        controls.steer[0] = unit(rng); controls.steer[1] = unit(rng); controls.steer[2] = unit(rng);
        controls.throttle = unit(rng); controls.lift = unit(rng) * 0.5f + 0.5f; controls.value14 = unit(rng) * 0.2f;

        float out[3];
        forces(out, original, velocity, forward, &controls.steer[0]);
        const cS3DVector3 mine = port.CalculateForces({velocity[0], velocity[1], velocity[2]},
                                                      {forward[0], forward[1], forward[2]}, controls);
        Check(Same(out[0], mine.x) && Same(out[1], mine.y) && Same(out[2], mine.z), "CalculateForces", t);

        Transform tOriginal{};
        tOriginal.counter = 7;
        float heading[3] = {0, 0, 0};
        const float delta = 0.01f + 0.2f * (unit(rng) + 1);
        orient(original, velocity, heading, &controls.steer[0], &tOriginal, delta);
        cS3DTransform tPort;
        tPort.changeCounter = 7;
        tPort.flags = 0;
        cS3DVector3 headingPort;
        port.UpdateOrientation({velocity[0], velocity[1], velocity[2]}, headingPort, controls, tPort, delta);
        bool same = Same(heading[0], headingPort.x) && Same(heading[1], headingPort.y) && Same(heading[2], headingPort.z) &&
                    tOriginal.counter == tPort.changeCounter && tOriginal.flags == tPort.flags &&
                    Same(Field(0x378), port.Roll()) && Same(Field(0x37c), port.Pitch()) && Same(Field(0x380), port.Yaw());
        for (int i = 0; i < 9; ++i) same = same && Same(tOriginal.rotation[i], tPort.rotation[i / 3][i % 3]);
        Check(same, "UpdateOrientation", t);

        {   // AI_Stop
            float c[6], tune[12], o[3], v[3];
            for (float& x : c) x = unit(rng);
            for (float& x : tune) x = 0.05f + 0.5f * (unit(rng) + 1);
            for (float& x : o) x = unit(rng);
            for (float& x : v) x = speed(rng) * (t % 5 == 0 ? 0.001f : 1.0f);
            std::memcpy(original + 0x314, c, sizeof c);
            std::memcpy(original + 0x438, tune, sizeof tune);
            std::memcpy(original + 0x8c, o, sizeof o);
            std::memcpy(original + 0x36c, v, sizeof v);
            AirplaneOracleAccess::SetMotion(port, c, tune, o, v);
            const bool a = Function<bool (*)(void*, float)>(0x1006d3f70)(original, delta);
            const bool b = port.AI_Stop(delta);
            float mine[6];
            AirplaneOracleAccess::Controls(port, mine);
            bool same = a == b;
            for (int i = 0; i < 6; ++i) same = same && Same(Field(0x314 + 4 * i), mine[i]);
            Check(same, "AI_Stop", t);

            // AI_ApproachAltitude on the same motion state
            const float position[3] = {speed(rng), 100.0f + speed(rng) * (t % 3 == 0 ? 0.05f : 1.0f), speed(rng)};
            const int st = static_cast<int>(rng() % 4);
            for (float& x : c) x = unit(rng) * 1.3f;
            std::memcpy(original + 0x314, c, sizeof c);
            std::memcpy(original + 0x74, position, sizeof position);
            std::memcpy(original + 0x310, &st, 4);
            AirplaneOracleAccess::SetMotion(port, c, tune, o, v);
            AirplaneOracleAccess::SetPositionState(port, position, st);
            const bool lift = rng() & 1;
            const int ra = Function<int (*)(void*, float, bool, float)>(0x1006d4a0c)(original, 100.0f, lift, delta);
            const int rb = port.AI_ApproachAltitude(100.0f, lift, delta);
            AirplaneOracleAccess::Controls(port, mine);
            same = ra == rb;
            for (int i = 0; i < 6; ++i) same = same && Same(Field(0x314 + 4 * i), mine[i]);
            Check(same, "AI_ApproachAltitude", t);

            // the steering helpers: same motion state, random geometry
            const float pa[3] = {speed(rng), speed(rng) * 0.1f, speed(rng)};
            float pb[3] = {unit(rng), unit(rng) * 0.2f, unit(rng)};
            const float pe[3] = {unit(rng), unit(rng) * 0.2f, unit(rng)};
            if (t % 9 == 0) { pb[0] = o[0] + 0.01f * unit(rng); pb[2] = o[2]; }
            auto reset = [&] {
                std::memcpy(original + 0x314, c, sizeof c);
                AirplaneOracleAccess::SetMotion(port, c, tune, o, v);
            };
            auto compare = [&](bool ra2, bool rb2, const char* what) {
                AirplaneOracleAccess::Controls(port, mine);
                bool ok = ra2 == rb2;
                for (int i = 0; i < 6; ++i) ok = ok && Same(Field(0x314 + 4 * i), mine[i]);
                Check(ok, what, t);
            };
            const cS3DVector3 va{pa[0], pa[1], pa[2]}, vb{pb[0], pb[1], pb[2]}, ve{pe[0], pe[1], pe[2]};
            reset();
            compare(Function<bool (*)(void*, const float*, const float*, float)>(0x1006d51a2)(original, pa, pb, delta),
                    port.AI_FollowRay(va, vb, delta), "AI_FollowRay");
            reset();
            compare(Function<bool (*)(void*, const float*, float)>(0x1006d4852)(original, pb, delta),
                    port.AI_TurnToVector(vb, delta), "AI_TurnToVector");
            reset();
            Function<void (*)(void*, const float*, float)>(0x1006d44cc)(original, pa, delta);
            port.AI_Taxi(va, delta);
            compare(true, true, "AI_Taxi");
            const int phase = static_cast<int>(rng() % 4);
            const float rate = 0.05f + 0.5f * (unit(rng) + 1);
            reset();
            std::memcpy(original + 0x434, &phase, 4);
            std::memcpy(original + 0x3cc, &rate, 4);
            AirplaneOracleAccess::Phase(port) = phase;
            AirplaneOracleAccess::SetTurnRate(port, rate);
            const bool fa = Function<bool (*)(void*, const float*, const float*, const float*, float)>(0x1006d4df8)(
                original, pb, pa, pe, delta);
            const bool fb = port.AI_FollowVectorAndTurn(vb, va, ve, delta);
            int phaseOriginal;
            std::memcpy(&phaseOriginal, original + 0x434, 4);
            Check(phaseOriginal == AirplaneOracleAccess::Phase(port), "AI_FollowVectorAndTurn phase", t);
            compare(fa, fb, "AI_FollowVectorAndTurn");
        }

        if (t % 8 == 0) {
            SetOriginal(f, 3);
            AirplaneOracleAccess::Set(port, f, 3);
            Check(Same(climb(original), port.CalculateClimbSlope()), "CalculateClimbSlope", t);
            SetOriginal(f, 3);
            AirplaneOracleAccess::Set(port, f, 3);
            Check(Same(descent(original), port.CalculateDescentSlope()), "CalculateDescentSlope", t);
            SetOriginal(f, 3);
            AirplaneOracleAccess::Set(port, f, 3);
            Check(Same(turn(original), port.CalculateTurnRate()), "CalculateTurnRate", t);
        }
    }
    std::printf("airplane_oracle: %d trials, %d mismatches\n", trials, failures);
    return failures ? 1 : 0;
}
