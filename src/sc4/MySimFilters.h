// cMySimBuildingFilter and cMySimPylonFilter — occupant filters the MySim code uses to find buildings a MySim may move into and power
// pylons (3 functions each: IsOccupantIncluded 0x1007aaf18 / 0x1006b88f6, destructors 0x1007ab008/0x1007ab012 and 0x1006b89e2/0x1006b89ec;
// both derive cSC4OccupantFilter and add nothing but +0x14 on the building filter).
//
// Building filter: the occupant must be a building (type 0x278128a0) that answers interface 0x87dda3a9, whose lot (looked up with the
// building's lot key in the lot manager) is missing or below stage 4, whose completion is at least 100%, and - when the filter flag at
// +0x14 is set - whose kind is 1.
// Pylon filter: the occupant must be of type 0xa9b6d7de and answer interface 0xc9b6d7d6; that interface must report a non-zero kind and
// not the "blocked" flag, and its linked object (slot 0xa0) must answer interface 0x0a1da42f.
#pragma once
#include "sc4/OccupantManager.h"

namespace sc4 {

constexpr std::uint32_t kOccupantTypeBuilding = 0x278128a0;
constexpr std::uint32_t kOccupantTypePylon = 0xa9b6d7de;
constexpr std::uint32_t kIidMySimBuilding = 0x87dda3a9;
constexpr std::uint32_t kIidMySimPylon = 0xc9b6d7d6;
constexpr std::uint32_t kIidPylonLink = 0x0a1da42f;

// The building interface 0x87dda3a9 as the filter uses it.
class IMySimBuildingFacts : public cIGZUnknown {
public:
    virtual void Slot3() {}
    virtual std::uint32_t LotKey() = 0;                  // vslot 0x18
    virtual const float* Completion() = 0;               // vslot 0x48 (1.0 = 100%)
    virtual const int* OccupancyKind() = 0;              // vslot 0x58
};
// The pylon interface 0xc9b6d7d6.
class IMySimPylonFacts : public cIGZUnknown {
public:
    virtual int Kind() = 0;                              // vslot 0x38
    virtual bool Blocked() = 0;                          // vslot 0x80
    virtual cIGZUnknown* LinkedObject() = 0;             // vslot 0xa0
};

class IMySimFilterHost {
public:
    virtual ~IMySimFilterHost() = default;
    // SL::spLotManager vslot 0x70(lotKey) -> lot; the lot's vslot 0x118 is its stage. False when there is no such lot.
    virtual bool LotStage(std::uint32_t lotKey, std::uint8_t& stage) = 0;
};
IMySimFilterHost* MySimFilterHost();
void SetMySimFilterHost(IMySimFilterHost* host);

class cMySimBuildingFilter : public cSC4OccupantFilter {
public:
    explicit cMySimBuildingFilter(bool onlyKindOne = false) : onlyKindOne_(onlyKindOne) {}
    ~cMySimBuildingFilter() override = default;           // @ 0x1007ab008 (deleting 0x1007ab012)
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;   // @ 0x1007aaf18
private:
    bool onlyKindOne_ = false;                            // +0x14
};

class cMySimPylonFilter : public cSC4OccupantFilter {
public:
    ~cMySimPylonFilter() override = default;              // @ 0x1006b89e2 (deleting 0x1006b89ec)
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;   // @ 0x1006b88f6
};

}  // namespace sc4
