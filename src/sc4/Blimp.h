// cSC4Blimp — the city blimp (vtable 0x100ae42f0), a cSC4Aircraft that floats toward a random point of the city and picks a new
// one. Layout after cSC4Aircraft (+0x310): float 1/180 (0x3bb60b61), then a cS3DVector3 heading at +0x314 and 5 more zero bytes.
// RECOVERED: constructor, Init, DoAnimationTick (does nothing), GetNewDirection, GetModelResourceInstance.
#pragma once
#include "sc4/Aircraft.h"

namespace sc4 {

constexpr std::uint32_t kBlimpModelResourceInstance = 0x0818f120;   // 0x1006d6790
constexpr float kBlimpRate = 0.0055555556900799274f;               // 0x3bb60b61 (1/180), +0x310

class cSC4Blimp : public cSC4Aircraft {
public:
    explicit cSC4Blimp(IAutomatonHost* host = nullptr, IAircraftHost* aircraftHost = nullptr) : cSC4Aircraft(host, aircraftHost) {}   // @ 0x1006d65ba
    bool Init() override;                                                     // 0x18  @ 0x1006d6640
    bool DoAnimationTick(std::uint32_t data, float milliseconds) override { (void)data; (void)milliseconds; return true; }   // 0x278 @ 0x1006d6692
    // 0x448: a unit vector from `from` to a random point of the city at the same height (the sum of squares keeps the
    // original's grouping (x*x + y*y) + z*z, which Ghidra showed reversed).
    void GetNewDirection(const cS3DVector3& from, cS3DVector3& out);          // @ 0x1006d669a
    std::uint32_t GetModelResourceInstance() const { return kBlimpModelResourceInstance; }   // 0x450 @ 0x1006d6790
    const cS3DVector3& Heading() const { return heading_; }

private:
    float rate_ = kBlimpRate;               // +0x310
    cS3DVector3 heading_{};                 // +0x314
};

}  // namespace sc4
