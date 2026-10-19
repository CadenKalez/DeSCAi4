// More small occupant filters (cSC4AnimatableOccupantFilter 0x100359d32, cSC4VisibleAnimatableOccupantFilter
// 0x100359d7c, cSC4ActiveFireOccupantFilter 0x10045e770, cSC4AudioAmbienceManagerOccupantFilter 0x1003ec5a6,
// cSC4PlaceLotOccupantFilter 0x1003c1f5a, cSC4FloraBlockingOccupantFilter 0x1000c254a).
#pragma once
#include <cstdint>
#include <functional>

#include "sc4/FireOccupant.h"
#include "sc4/OccupantManager.h"

namespace sc4 {

constexpr std::uint32_t kPropertyNotAnimatable = 0x4a70d491;
constexpr std::uint32_t kModelClassAnimatableA = 0xea123be1, kModelClassAnimatableB = 0xaa123bf9;
constexpr std::uint32_t kOccupantTypeProp = 0xa823821e, kOccupantTypeNetwork = 0xc772bf98;

// cSC4ModelMakerUtility::HasOccupantModelClass(occupant, classIds, count)
using ModelClassTest = std::function<bool(cISC4Occupant* occupant, const std::uint32_t* classIds, int count)>;
void SetModelClassTest(ModelClassTest test);

// Occupants whose model is one of the two animatable classes and that do not carry property 0x4a70d491.
class cSC4AnimatableOccupantFilter : public cSC4OccupantFilter {
public:
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;
};
// The same, and the occupant must also have flag 4 (visible) set.
class cSC4VisibleAnimatableOccupantFilter : public cSC4OccupantFilter {
public:
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;
};
// Fire occupants (type 0x69d25c5f) that are attached to a target.
class cSC4ActiveFireOccupantFilter : public cSC4OccupantFilter {
public:
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;
};
// Props, buildings, flora and the four vehicle/pedestrian types the ambience manager listens to. A bare counted object
// (count at +8, interface 0x46a80026), not serializable.
class cSC4AudioAmbienceManagerOccupantFilter final : public cISC4OccupantFilter {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool IsOccupantIncluded(cISC4Occupant* occupant) override { return IsOccupantTypeIncluded(occupant->GetType()); }
    bool IsOccupantTypeIncluded(std::uint32_t type) override;
    bool IsPropertyHolderIncluded(cISCPropertyHolder*) override { return true; }
private:
    std::uint32_t refCount_ = 0;
};
// Everything except the two types lots may be placed over (0x0990b2ea, 0x2890d4de).
class cSC4PlaceLotOccupantFilter : public cSC4OccupantFilter {
public:
    bool IsOccupantTypeIncluded(std::uint32_t type) override { return type != 0x0990b2ea && type != 0x2890d4de; }
};
// Props, buildings, landfill tiles and flora block flora; networks never do. Not serializable (Write fails).
class cSC4FloraBlockingOccupantFilter : public cSC4OccupantFilter {
public:
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;
    bool Write(cIGZOStream&) const override { return false; }
};

}  // namespace sc4
